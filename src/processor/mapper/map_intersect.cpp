#include "planner/logical_plan/logical_operator/logical_intersect.h"
#include "processor/mapper/plan_mapper.h"
#include "processor/operator/intersect/intersect.h"
#include "processor/operator/intersect/intersect_build.h"

using namespace kuzu::planner;

namespace kuzu {
namespace processor {

std::unique_ptr<PhysicalOperator> PlanMapper::mapLogicalIntersectToPhysical(
    LogicalOperator* logicalOperator) {
    auto logicalIntersect = (LogicalIntersect*)logicalOperator;
    auto outSchema = logicalIntersect->getSchema();
    std::vector<std::unique_ptr<PhysicalOperator>> children;
    children.resize(logicalOperator->getNumChildren());
    std::vector<std::shared_ptr<IntersectSharedState>> sharedStates;
    std::vector<IntersectDataInfo> intersectDataInfos;
    // Map build side children.
    for (auto i = 1u; i < logicalIntersect->getNumChildren(); i++) {
        auto keyNodeID = logicalIntersect->getKeyNodeID(i - 1);
        auto buildSchema = logicalIntersect->getChild(i)->getSchema();
        auto buildSidePrevOperator = mapLogicalOperatorToPhysical(logicalIntersect->getChild(i));
        std::vector<DataPos> payloadsDataPos;
        auto buildDataInfo =
            generateBuildDataInfo(*buildSchema, {keyNodeID}, buildSchema->getExpressionsInScope());
        for (auto& [dataPos, _] : buildDataInfo.payloadsPosAndType) {
            auto expression = buildSchema->getGroup(dataPos.dataChunkPos)
                                  ->getExpressions()[dataPos.valueVectorPos];
            if (expression->getUniqueName() ==
                logicalIntersect->getIntersectNodeID()->getUniqueName()) {
                continue;
            }
            payloadsDataPos.emplace_back(outSchema->getExpressionPos(*expression));
        }
        auto sharedState = std::make_shared<IntersectSharedState>();
        sharedStates.push_back(sharedState);
        children[i] = make_unique<IntersectBuild>(
            std::make_unique<ResultSetDescriptor>(*buildSchema), sharedState, buildDataInfo,
            std::move(buildSidePrevOperator), getOperatorID(), keyNodeID->toString());
        IntersectDataInfo info{DataPos(outSchema->getExpressionPos(*keyNodeID)), payloadsDataPos};
        intersectDataInfos.push_back(info);
    }
    // Map probe side child.
    children[0] = mapLogicalOperatorToPhysical(logicalIntersect->getChild(0));
    // Map intersect.
    auto outputDataPos =
        DataPos(outSchema->getExpressionPos(*logicalIntersect->getIntersectNodeID()));
    auto intersect = make_unique<Intersect>(outputDataPos, intersectDataInfos, sharedStates,
        std::move(children), getOperatorID(), logicalIntersect->getExpressionsForPrinting());
    if (logicalIntersect->getSIP() == SidewaysInfoPassing::LEFT_TO_RIGHT) {
        mapASP(intersect.get());
    }
    return intersect;
}

} // namespace processor
} // namespace kuzu
