#include "planner/logical_plan/logical_operator/logical_add_property.h"
#include "planner/logical_plan/logical_operator/logical_copy.h"
#include "planner/logical_plan/logical_operator/logical_create_node_table.h"
#include "planner/logical_plan/logical_operator/logical_create_rel_table.h"
#include "planner/logical_plan/logical_operator/logical_drop_property.h"
#include "planner/logical_plan/logical_operator/logical_drop_table.h"
#include "planner/logical_plan/logical_operator/logical_rename_property.h"
#include "planner/logical_plan/logical_operator/logical_rename_table.h"
#include "processor/mapper/plan_mapper.h"
#include "processor/operator/copy/copy_node.h"
#include "processor/operator/copy/copy_rel.h"
#include "processor/operator/ddl/add_node_property.h"
#include "processor/operator/ddl/add_rel_property.h"
#include "processor/operator/ddl/create_node_table.h"
#include "processor/operator/ddl/create_rel_table.h"
#include "processor/operator/ddl/drop_property.h"
#include "processor/operator/ddl/drop_table.h"
#include "processor/operator/ddl/rename_property.h"
#include "processor/operator/ddl/rename_table.h"

namespace kuzu {
namespace processor {

static DataPos getOutputPos(LogicalDDL* logicalDDL) {
    auto outSchema = logicalDDL->getSchema();
    auto outputExpression = logicalDDL->getOutputExpression();
    return DataPos(outSchema->getExpressionPos(*outputExpression));
}

unique_ptr<PhysicalOperator> PlanMapper::mapLogicalCreateNodeTableToPhysical(
    LogicalOperator* logicalOperator) {
    auto createNodeTable = (LogicalCreateNodeTable*)logicalOperator;
    return make_unique<CreateNodeTable>(catalog, createNodeTable->getTableName(),
        createNodeTable->getPropertyNameDataTypes(), createNodeTable->getPrimaryKeyIdx(),
        getOutputPos(createNodeTable), getOperatorID(),
        createNodeTable->getExpressionsForPrinting(),
        &storageManager.getNodesStore().getNodesStatisticsAndDeletedIDs());
}

unique_ptr<PhysicalOperator> PlanMapper::mapLogicalCreateRelTableToPhysical(
    LogicalOperator* logicalOperator) {
    auto createRelTable = (LogicalCreateRelTable*)logicalOperator;
    return make_unique<CreateRelTable>(catalog, createRelTable->getTableName(),
        createRelTable->getPropertyNameDataTypes(), createRelTable->getRelMultiplicity(),
        createRelTable->getSrcDstTableIDs(), getOutputPos(createRelTable), getOperatorID(),
        createRelTable->getExpressionsForPrinting(),
        &storageManager.getRelsStore().getRelsStatistics());
}

unique_ptr<PhysicalOperator> PlanMapper::mapLogicalCopyToPhysical(
    LogicalOperator* logicalOperator) {
    auto copy = (LogicalCopy*)logicalOperator;
    auto tableName = catalog->getReadOnlyVersion()->getTableName(copy->getTableID());
    auto nodesStatistics = &storageManager.getNodesStore().getNodesStatisticsAndDeletedIDs();
    auto relsStatistics = &storageManager.getRelsStore().getRelsStatistics();
    if (catalog->getReadOnlyVersion()->containNodeTable(tableName)) {
        return make_unique<CopyNode>(catalog, copy->getCopyDescription(), copy->getTableID(),
            storageManager.getWAL(), nodesStatistics, getOperatorID(),
            copy->getExpressionsForPrinting());
    } else {
        return make_unique<CopyRel>(catalog, copy->getCopyDescription(), copy->getTableID(),
            storageManager.getWAL(), nodesStatistics, relsStatistics, getOperatorID(),
            copy->getExpressionsForPrinting());
    }
}

unique_ptr<PhysicalOperator> PlanMapper::mapLogicalDropTableToPhysical(
    LogicalOperator* logicalOperator) {
    auto dropTable = (LogicalDropTable*)logicalOperator;
    return make_unique<DropTable>(catalog, dropTable->getTableID(), getOutputPos(dropTable),
        getOperatorID(), dropTable->getExpressionsForPrinting());
}

unique_ptr<PhysicalOperator> PlanMapper::mapLogicalRenameTableToPhysical(
    LogicalOperator* logicalOperator) {
    auto renameTable = (LogicalRenameTable*)logicalOperator;
    return make_unique<RenameTable>(catalog, renameTable->getTableID(), renameTable->getNewName(),
        getOutputPos(renameTable), getOperatorID(), renameTable->getExpressionsForPrinting());
}

unique_ptr<PhysicalOperator> PlanMapper::mapLogicalAddPropertyToPhysical(
    LogicalOperator* logicalOperator) {
    auto addProperty = (LogicalAddProperty*)logicalOperator;
    auto expressionEvaluator =
        expressionMapper.mapExpression(addProperty->getDefaultValue(), *addProperty->getSchema());
    if (catalog->getReadOnlyVersion()->containNodeTable(addProperty->getTableID())) {
        return make_unique<AddNodeProperty>(catalog, addProperty->getTableID(),
            addProperty->getPropertyName(), addProperty->getDataType(),
            std::move(expressionEvaluator), storageManager, getOutputPos(addProperty),
            getOperatorID(), addProperty->getExpressionsForPrinting());
    } else {
        return make_unique<AddRelProperty>(catalog, addProperty->getTableID(),
            addProperty->getPropertyName(), addProperty->getDataType(),
            std::move(expressionEvaluator), storageManager, getOutputPos(addProperty),
            getOperatorID(), addProperty->getExpressionsForPrinting());
    }
}

unique_ptr<PhysicalOperator> PlanMapper::mapLogicalDropPropertyToPhysical(
    LogicalOperator* logicalOperator) {
    auto dropProperty = (LogicalDropProperty*)logicalOperator;
    return make_unique<DropProperty>(catalog, dropProperty->getTableID(),
        dropProperty->getPropertyID(), getOutputPos(dropProperty), getOperatorID(),
        dropProperty->getExpressionsForPrinting());
}

unique_ptr<PhysicalOperator> PlanMapper::mapLogicalRenamePropertyToPhysical(
    LogicalOperator* logicalOperator) {
    auto renameProperty = (LogicalRenameProperty*)logicalOperator;
    return make_unique<RenameProperty>(catalog, renameProperty->getTableID(),
        renameProperty->getPropertyID(), renameProperty->getNewName(), getOutputPos(renameProperty),
        getOperatorID(), renameProperty->getExpressionsForPrinting());
}

} // namespace processor
} // namespace kuzu
