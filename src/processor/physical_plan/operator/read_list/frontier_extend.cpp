#include "src/processor/include/physical_plan/operator/read_list/frontier_extend.h"

#include <omp.h>

namespace graphflow {
namespace processor {

static uint64_t getNextPowerOfTwo(uint64_t value);

FrontierExtend::FrontierExtend(uint64_t inDataChunkPos, uint64_t inValueVectorPos, AdjLists* lists,
    label_t outNodeIDVectorLabel, uint64_t lowerBound, uint64_t upperBound,
    unique_ptr<PhysicalOperator> prevOperator, ExecutionContext& context, uint32_t id)
    : ReadList{inDataChunkPos, inValueVectorPos, lists, move(prevOperator), context, id},
      startLayer{lowerBound}, endLayer{upperBound}, outNodeIDVectorLabel{outNodeIDVectorLabel} {
    operatorType = FRONTIER_EXTEND;
    outValueVector =
        make_shared<NodeIDVector>(outNodeIDVectorLabel, NodeIDCompressionScheme(), false);
    outDataChunk = make_shared<DataChunk>();
    outDataChunk->append(outValueVector);
    outValueVector->state->initMultiplicity();
    resultSet->append(outDataChunk, make_shared<ListSyncState>());
    uint64_t maxNumThreads = omp_get_max_threads();
    vectors.reserve(maxNumThreads);
    listsPageHandles.reserve(maxNumThreads);
    for (auto i = 0u; i < maxNumThreads; i++) {
        vectors.push_back(make_shared<NodeIDVector>(0, lists->getNodeIDCompressionScheme(), false));
        vectors[i]->state = make_shared<SharedVectorState>(DEFAULT_VECTOR_CAPACITY);
        listsPageHandles.push_back(make_unique<ListsPageHandle>());
        listsPageHandles[i]->setListSyncState(make_shared<ListSyncState>());
        listsPageHandles[i]->setIsAdjListHandle();
    }
    frontierPerLayer.reserve(upperBound + 1);
    threadLocalFrontierPerLayer.reserve(upperBound);
    for (auto i = 0u; i < upperBound; i++) {
        frontierPerLayer.push_back(nullptr);
        threadLocalFrontierPerLayer.emplace_back();
        for (auto j = 0u; j < maxNumThreads; j++) {
            threadLocalFrontierPerLayer[i].push_back(nullptr);
        }
    }
    frontierPerLayer.push_back(nullptr);
    currOutputPos.hasMoreTuplesToProduce = false;
}

void FrontierExtend::getNextTuples() {
    metrics->executionTime.start();
    if (currOutputPos.hasMoreTuplesToProduce) {
        produceOutputTuples();
        metrics->executionTime.stop();
        return;
    }
    do {
        prevOperator->getNextTuples();
        if (inNodeIDVector->state->size == 0) {
            outDataChunk->state->size = 0;
            metrics->executionTime.stop();
            return;
        }
    } while (computeFrontiers());
    currOutputPos.reset(startLayer - 1);
    produceOutputTuples();
    metrics->executionTime.stop();
}

bool FrontierExtend::computeFrontiers() {
    frontierPerLayer[0] = createInitialFrontierSet();
    for (auto layer = 0u; layer < endLayer; layer++) {
        if (frontierPerLayer[layer] == nullptr) {
            break;
        }
        extendToThreadLocalFrontiers(layer);
        createGlobalFrontierFromThreadLocalFrontiers(layer);
    }
    return frontierPerLayer[startLayer - 1] == nullptr;
}

void FrontierExtend::extendToThreadLocalFrontiers(uint64_t layer) {
    if (frontierPerLayer[layer] == nullptr) {
        frontierPerLayer[layer + 1] = nullptr;
        return;
    }
    for (auto blockIdx = 0u; blockIdx < frontierPerLayer[layer]->hashTableBlocks.size();
         blockIdx++) {
        auto mainBlock = frontierPerLayer[layer]->hashTableBlocks[blockIdx];
        std::atomic<uint64_t> currSlot{0u};
#pragma omp parallel
        {
            auto threadId = omp_get_thread_num();
            threadLocalFrontierPerLayer[layer][threadId] = createFrontierBag();
            uint64_t slotToExtendFrom;
            while (true) {
                slotToExtendFrom = currSlot++;
                if (slotToExtendFrom >= NUM_SLOTS_PER_BLOCK_SET) {
                    break;
                }
                if (mainBlock[slotToExtendFrom].hasValue) {
                    auto slot = &mainBlock[slotToExtendFrom];
                    while (slot) {
                        do {
                            // Warning: we use non-thread-safe metric inside openmp parallelization
                            // metrics won't be exactly but hopefully good enough.
                            lists->readValues(slot->nodeOffset, vectors[threadId],
                                listsPageHandles[threadId], MAX_TO_READ,
                                *metrics->bufferManagerMetrics);
                            threadLocalFrontierPerLayer[layer][threadId]->append(
                                *(NodeIDVector*)(vectors[threadId].get()), slot->multiplicity);
                        } while (listsPageHandles[threadId]->hasMoreToRead());
                        lists->reclaim(*listsPageHandles[threadId]);
                        slot = slot->next;
                    }
                }
            }
        }
    }
}

void FrontierExtend::createGlobalFrontierFromThreadLocalFrontiers(uint64_t layer) {
    frontierPerLayer[layer + 1] = makeFrontierSet(layer);
    for (auto threadId = 0; threadId < omp_get_max_threads(); threadId++) {
        // ensure the omp thread with given Id created a local frontier.
        if (threadLocalFrontierPerLayer[layer][threadId] == nullptr) {
            continue;
        }
        auto& localFrontier = threadLocalFrontierPerLayer[layer][threadId];
        std::atomic<uint64_t> currSlot{0u};
#pragma omp parallel
        {
            uint64_t slotToInsert;
            while (true) {
                // We expect many nodes per slot and hence each omp thread grabs a slot at a time.
                slotToInsert = currSlot++;
                if (slotToInsert > NUM_SLOTS_BAG) {
                    break;
                }
                // We do not synchronize as each slot from the localFrontier maps to a unique set of
                // slots in the outout layer frontier.
                frontierPerLayer[layer + 1]->insert(*localFrontier, slotToInsert);
            }
        }
        delete localFrontier;
        localFrontier = nullptr;
    }
}

void FrontierExtend::produceOutputTuples() {
    auto outNodeIDs = (node_offset_t*)outValueVector->values;
    outValueVector->state->size = 0;
    while (currOutputPos.layer < endLayer) {
        auto frontier = frontierPerLayer[currOutputPos.layer];
        if (frontier == nullptr) {
            break;
        }
        while (currOutputPos.blockIdx < frontier->hashTableBlocks.size()) {
            auto mainBlock = frontier->hashTableBlocks[currOutputPos.blockIdx];
            auto maxSlot = NUM_SLOTS_PER_BLOCK_SET;
            while (outValueVector->state->size < DEFAULT_VECTOR_CAPACITY &&
                   currOutputPos.slot < maxSlot) {
                if (mainBlock[currOutputPos.slot].hasValue) {
                    auto slot = &mainBlock[currOutputPos.slot];
                    while (slot) {
                        outNodeIDs[outValueVector->state->size] = slot->nodeOffset;
                        outValueVector->state->multiplicity[outValueVector->state->size++] =
                            slot->multiplicity;
                        slot = slot->next;
                        if (outValueVector->state->size == DEFAULT_VECTOR_CAPACITY) {
                            currOutputPos.hasMoreTuplesToProduce = true;
                            return;
                        }
                    }
                }
                currOutputPos.slot++;
            }
            currOutputPos.slot = 0u;
            currOutputPos.blockIdx++;
        }
        currOutputPos.blockIdx = 0u;
        currOutputPos.layer++;
    }
    for (auto& frontier : frontierPerLayer) {
        delete frontier;
        frontier = nullptr;
    }
    metrics->numOutputTuple.increase(outDataChunk->state->size);
}

FrontierSet* FrontierExtend::createInitialFrontierSet() {
    // We assume the inNodeIDVector to be flat similar to AdjListExtend's assumption.
    auto pos = inNodeIDVector->state->getPositionOfCurrIdx();
    auto nodeOffset = inNodeIDVector->readNodeOffset(pos);
    auto numSlots = getNextPowerOfTwo(lists->getNumElementsInList(nodeOffset));
    if (numSlots == 0) {
        return nullptr;
    }
    auto frontier = new FrontierSet();
    frontier->setMemoryManager(context.memoryManager);
    frontier->initHashTable(numSlots);
    do {
        lists->readValues(nodeOffset, vectors[0], listsPageHandles[0], MAX_TO_READ,
            *metrics->bufferManagerMetrics);
        frontier->insert(*vectors[0]);
    } while (listsPageHandles[0]->hasMoreToRead());
    lists->reclaim(*listsPageHandles[0]);
    return frontier;
}

FrontierSet* FrontierExtend::makeFrontierSet(uint64_t layer) {
    auto numNodeOffsets = 0u;
    for (auto& localFrontier : threadLocalFrontierPerLayer[layer]) {
        if (localFrontier == nullptr) {
            continue;
        }
        numNodeOffsets += localFrontier->size;
    }
    auto numSlots = getNextPowerOfTwo(numNodeOffsets);
    if (numSlots == 0) {
        return nullptr;
    }
    auto frontier = new FrontierSet();
    frontier->setMemoryManager(context.memoryManager);
    frontier->initHashTable(numSlots < NUM_SLOTS_BAG ? NUM_SLOTS_BAG : numSlots);
    return frontier;
}

FrontierBag* FrontierExtend::createFrontierBag() {
    auto frontierBag = new FrontierBag();
    frontierBag->setMemoryManager(context.memoryManager);
    frontierBag->initHashTable();
    return frontierBag;
}

unique_ptr<PhysicalOperator> FrontierExtend::clone() {
    auto cloneOp = make_unique<FrontierExtend>(inDataChunkPos, inValueVectorPos, (AdjLists*)lists,
        outNodeIDVectorLabel, startLayer, endLayer, prevOperator->clone(), context, id);
    return cloneOp;
}

static uint64_t getNextPowerOfTwo(uint64_t value) {
    value--;
    value |= value >> 1u;
    value |= value >> 2u;
    value |= value >> 4u;
    value |= value >> 8u;
    value |= value >> 16u;
    value++;
    return value;
}

} // namespace processor
} // namespace graphflow
