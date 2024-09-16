#include "processor/operator/scan/scan_table.h"

namespace kuzu {
namespace processor {

void ScanTable::initLocalStateInternal(ResultSet* resultSet, ExecutionContext*) {
    IDVector = resultSet->getValueVector(info.IDPos).get();
    if (info.outVectorsPos.empty()) {
        outState = IDVector->state.get();
    } else {
        outState = resultSet->getValueVector(info.outVectorsPos[0])->state.get();
    }
}

void ScanTable::initVectors(storage::TableScanState& state, const ResultSet& resultSet) const {
    state.nodeIDVector = resultSet.getValueVector(info.IDPos).get();
    state.rowIdxVector->state = info.outVectorsPos.empty() ?
                                    state.nodeIDVector->state :
                                    resultSet.getValueVector(info.outVectorsPos[0])->state;
    state.outState = outState;
    for (auto& pos : info.outVectorsPos) {
        state.outputVectors.push_back(resultSet.getValueVector(pos).get());
    }
}

} // namespace processor
} // namespace kuzu
