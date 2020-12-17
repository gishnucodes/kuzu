#include "src/loader/include/rels_loader.h"

#include "src/loader/include/csv_reader.h"

namespace graphflow {
namespace loader {

const char RelsLoader::EMPTY_STRING = 0;

void RelsLoader::load(vector<string>& fnames, vector<uint64_t>& numBlocksPerFile) {
    RelLabelDescription description;
    for (auto relLabel = 0u; relLabel < catalog.getRelLabelsCount(); relLabel++) {
        auto& propertyMap = catalog.getPropertyMapForRelLabel(relLabel);
        description.propertyMap = &propertyMap;
        description.label = relLabel;
        description.fname = fnames[relLabel];
        description.numBlocks = numBlocksPerFile[relLabel];
        for (auto& dir : DIRS) {
            description.isSingleCardinalityPerDir[dir] =
                catalog.isSingleCaridinalityInDir(description.label, dir);
            description.nodeLabelsPerDir[dir] =
                catalog.getNodeLabelsForRelLabelDir(description.label, dir);
        }
        for (auto& dir : DIRS) {
            description.numBytesSchemePerDir[dir] =
                RelsStore::getNumBytesScheme(description.nodeLabelsPerDir[!dir],
                    graph.getNumNodesPerLabel(), catalog.getNodeLabelsCount());
        }
        loadRelsForLabel(description);
    }
}

void RelsLoader::loadRelsForLabel(RelLabelDescription& description) {
    logger->info("Processing relLabel {}.", description.label);
    AdjAndPropertyListsLoaderHelper adjAndPropertyListsLoaderHelper{
        description, threadPool, graph, catalog, outputDirectory, logger};
    constructAdjRelsAndCountRelsInAdjLists(description, adjAndPropertyListsLoaderHelper);
    if (!description.isSingleCardinalityPerDir[FWD] ||
        !description.isSingleCardinalityPerDir[BWD]) {
        constructAdjLists(description, adjAndPropertyListsLoaderHelper);
    }
    logger->info("Done.");
}

void RelsLoader::constructAdjRelsAndCountRelsInAdjLists(RelLabelDescription& description,
    AdjAndPropertyListsLoaderHelper& adjAndPropertyListsLoaderHelper) {
    AdjAndPropertyColumnsLoaderHelper adjAndPropertyColumnsLoaderHelper{
        description, threadPool, graph, catalog, outputDirectory, logger};
    logger->info("Populating AdjRels and Rel Property Columns...");
    for (auto blockId = 0u; blockId < description.numBlocks; blockId++) {
        threadPool.execute(populateAdjRelsAndCountRelsInAdjListsTask, &description, blockId,
            metadata.at("tokenSeparator").get<string>()[0], &adjAndPropertyListsLoaderHelper,
            &adjAndPropertyColumnsLoaderHelper, &nodeIDMaps, &catalog, logger);
    }
    threadPool.wait();
    if (description.hasProperties() && !description.requirePropertyLists()) {
        adjAndPropertyColumnsLoaderHelper.sortOverflowStrings();
    }
    adjAndPropertyColumnsLoaderHelper.saveToFile();
}

void RelsLoader::constructAdjLists(RelLabelDescription& description,
    AdjAndPropertyListsLoaderHelper& adjAndPropertyListsLoaderHelper) {
    adjAndPropertyListsLoaderHelper.buildAdjListsHeadersAndListsMetadata();
    adjAndPropertyListsLoaderHelper.buildInMemStructures();
    logger->info("Populating AdjLists and Rel Property Lists...");
    for (auto blockId = 0u; blockId < description.numBlocks; blockId++) {
        threadPool.execute(populateAdjListsTask, &description, blockId,
            metadata.at("tokenSeparator").get<string>()[0], &adjAndPropertyListsLoaderHelper,
            &nodeIDMaps, &catalog, logger);
    }
    threadPool.wait();
    if (description.requirePropertyLists()) {
        adjAndPropertyListsLoaderHelper.sortOverflowStrings();
    }
    adjAndPropertyListsLoaderHelper.saveToFile();
}

void RelsLoader::populateAdjRelsAndCountRelsInAdjListsTask(RelLabelDescription* description,
    uint64_t blockId, const char tokenSeparator,
    AdjAndPropertyListsLoaderHelper* adjAndPropertyListsLoaderHelper,
    AdjAndPropertyColumnsLoaderHelper* adjAndPropertyColumnsLoaderHelper,
    vector<shared_ptr<NodeIDMap>>* nodeIDMaps, const Catalog* catalog,
    shared_ptr<spdlog::logger> logger) {
    logger->debug("start {0} {1}", description->fname, blockId);
    CSVReader reader(description->fname, tokenSeparator, blockId);
    if (0 == blockId) {
        if (reader.hasNextLine()) {
            reader.skipLine(); // skip header line
        }
    }
    vector<bool> requireToReadLabels{true, true};
    vector<nodeID_t> nodeIDs{2};
    vector<PageCursor> stringOverflowCursors{description->propertyMap->size()};
    for (auto& dir : DIRS) {
        requireToReadLabels[dir] = 1 != description->nodeLabelsPerDir[dir].size();
        nodeIDs[dir].label = description->nodeLabelsPerDir[dir][0];
    }
    while (reader.hasNextLine()) {
        inferLabelsAndOffsets(reader, nodeIDs, nodeIDMaps, catalog, requireToReadLabels);
        for (auto& dir : DIRS) {
            if (description->isSingleCardinalityPerDir[dir]) {
                adjAndPropertyColumnsLoaderHelper->setRel(dir, nodeIDs);
            } else {
                adjAndPropertyListsLoaderHelper->incrementListSize(dir, nodeIDs[dir]);
            }
        }
        if (description->hasProperties() && !description->requirePropertyLists()) {
            if (description->isSingleCardinalityPerDir[FWD]) {
                putPropsOfLineIntoInMemPropertyColumns((*description).propertyMap, reader,
                    adjAndPropertyColumnsLoaderHelper, nodeIDs[FWD], stringOverflowCursors, logger);
            } else if (description->isSingleCardinalityPerDir[BWD]) {
                putPropsOfLineIntoInMemPropertyColumns((*description).propertyMap, reader,
                    adjAndPropertyColumnsLoaderHelper, nodeIDs[BWD], stringOverflowCursors, logger);
            }
        }
    }
    logger->debug("end   {0} {1}", description->fname, blockId);
}

void RelsLoader::populateAdjListsTask(RelLabelDescription* description, uint64_t blockId,
    const char tokenSeparator, AdjAndPropertyListsLoaderHelper* adjAndPropertyListsLoaderHelper,
    vector<shared_ptr<NodeIDMap>>* nodeIDMaps, const Catalog* catalog,
    shared_ptr<spdlog::logger> logger) {
    logger->debug("start {0} {1}", description->fname, blockId);
    CSVReader reader(description->fname, tokenSeparator, blockId);
    if (0 == blockId) {
        if (reader.hasNextLine()) {
            reader.skipLine(); // skip header line
        }
    }
    vector<bool> requireToReadLabels{true, true};
    vector<nodeID_t> nodeIDs{2};
    vector<uint64_t> reversePos{2};
    vector<vector<PageCursor>> stringOverflows{2};
    for (auto& dir : DIRS) {
        requireToReadLabels[dir] = 1 != description->nodeLabelsPerDir[dir].size();
        nodeIDs[dir].label = description->nodeLabelsPerDir[dir][0];
        stringOverflows[dir].resize(description->propertyMap->size());
    }
    while (reader.hasNextLine()) {
        inferLabelsAndOffsets(reader, nodeIDs, nodeIDMaps, catalog, requireToReadLabels);
        for (auto& dir : DIRS) {
            if (!description->isSingleCardinalityPerDir[dir]) {
                reversePos[dir] =
                    adjAndPropertyListsLoaderHelper->decrementListSize(dir, nodeIDs[dir]);
                adjAndPropertyListsLoaderHelper->setRel(reversePos[dir], dir, nodeIDs);
            }
        }
        if (description->requirePropertyLists()) {
            putPropsOfLineIntoInMemRelPropLists(description->propertyMap, reader, nodeIDs,
                reversePos, adjAndPropertyListsLoaderHelper, stringOverflows, logger);
        }
    }
    logger->debug("end   {0} {1}", description->fname, blockId);
}

void RelsLoader::inferLabelsAndOffsets(CSVReader& reader, vector<nodeID_t>& nodeIDs,
    vector<shared_ptr<NodeIDMap>>* nodeIDMaps, const Catalog* catalog,
    vector<bool>& requireToReadLabels) {
    for (auto& dir : DIRS) {
        reader.hasNextToken();
        if (requireToReadLabels[dir]) {
            nodeIDs[dir].label = (*catalog).getNodeLabelFromString(reader.getString());
        } else {
            reader.skipToken();
        }
        reader.hasNextToken();
        nodeIDs[dir].offset = (*(*nodeIDMaps)[nodeIDs[dir].label]).getOffset(reader.getString());
    }
}

void RelsLoader::putPropsOfLineIntoInMemPropertyColumns(const vector<Property>* propertyMap,
    CSVReader& reader, AdjAndPropertyColumnsLoaderHelper* adjAndPropertyColumnsLoaderHelper,
    const nodeID_t& nodeID, vector<PageCursor>& stringOverflows,
    shared_ptr<spdlog::logger> logger) {
    auto propertyIdx = 0u;
    while (reader.hasNextToken()) {
        switch ((*propertyMap)[propertyIdx].dataType) {
        case INT: {
            auto intVal = reader.skipTokenIfNull() ? NULL_INT : reader.getInteger();
            adjAndPropertyColumnsLoaderHelper->setProperty(
                nodeID, propertyIdx, reinterpret_cast<uint8_t*>(&intVal), INT);
            break;
        }
        case DOUBLE: {
            auto doubleVal = reader.skipTokenIfNull() ? NULL_DOUBLE : reader.getDouble();
            adjAndPropertyColumnsLoaderHelper->setProperty(
                nodeID, propertyIdx, reinterpret_cast<uint8_t*>(&doubleVal), DOUBLE);
            break;
        }
        case BOOL: {
            auto boolVal = reader.skipTokenIfNull() ? NULL_BOOL : reader.getBoolean();
            adjAndPropertyColumnsLoaderHelper->setProperty(
                nodeID, propertyIdx, reinterpret_cast<uint8_t*>(&boolVal), BOOL);
            break;
        }
        case STRING: {
            auto strVal = reader.skipTokenIfNull() ? &RelsLoader::EMPTY_STRING : reader.getString();
            adjAndPropertyColumnsLoaderHelper->setStringProperty(
                nodeID, propertyIdx, strVal, stringOverflows[propertyIdx]);
            break;
        }
        default:
            if (!reader.skipTokenIfNull()) {
                reader.skipToken();
            }
        }
        propertyIdx++;
    }
}

void RelsLoader::putPropsOfLineIntoInMemRelPropLists(const vector<Property>* propertyMap,
    CSVReader& reader, const vector<nodeID_t>& nodeIDs, const vector<uint64_t>& pos,
    AdjAndPropertyListsLoaderHelper* adjAndPropertyListsLoaderHelper,
    vector<vector<PageCursor>>& stringOvreflowCursors, shared_ptr<spdlog::logger> logger) {
    auto propertyIdx = 0;
    while (reader.hasNextToken()) {
        switch ((*propertyMap)[propertyIdx].dataType) {
        case INT: {
            auto intVal = reader.skipTokenIfNull() ? NULL_INT : reader.getInteger();
            for (auto& dir : DIRS) {
                adjAndPropertyListsLoaderHelper->setProperty(pos[dir], dir, nodeIDs[dir],
                    propertyIdx, reinterpret_cast<uint8_t*>(&intVal), INT);
            }
            break;
        }
        case DOUBLE: {
            auto doubleVal = reader.skipTokenIfNull() ? NULL_DOUBLE : reader.getDouble();
            for (auto& dir : DIRS) {
                adjAndPropertyListsLoaderHelper->setProperty(pos[dir], dir, nodeIDs[dir],
                    propertyIdx, reinterpret_cast<uint8_t*>(&doubleVal), DOUBLE);
            }
            break;
        }
        case BOOL: {
            auto boolVal = reader.skipTokenIfNull() ? NULL_BOOL : reader.getBoolean();
            for (auto& dir : DIRS) {
                adjAndPropertyListsLoaderHelper->setProperty(pos[dir], dir, nodeIDs[dir],
                    propertyIdx, reinterpret_cast<uint8_t*>(&boolVal), BOOL);
            }
            break;
        }
        case STRING: {
            auto strVal = reader.skipTokenIfNull() ? &EMPTY_STRING : reader.getString();
            for (auto& dir : DIRS) {
                adjAndPropertyListsLoaderHelper->setStringProperty(pos[dir], dir, nodeIDs[dir],
                    propertyIdx, strVal, stringOvreflowCursors[dir][propertyIdx]);
            }
        }
        default:
            if (!reader.skipTokenIfNull()) {
                reader.skipToken();
            }
        }
        propertyIdx++;
    }
}

} // namespace loader
} // namespace graphflow
