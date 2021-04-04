#include "src/processor/include/physical_plan/operator/list_reader/rel_property_list_reader.h"

#include "src/common/include/vector/operations/vector_comparison_operations.h"

using namespace graphflow::common;

namespace graphflow {
namespace processor {

RelPropertyListReader::RelPropertyListReader(uint64_t inDataChunkPos, uint64_t inValueVectorPos,
    uint64_t outDataChunkPos, BaseLists* lists, unique_ptr<PhysicalOperator> prevOperator)
    : ListReader{inDataChunkPos, inValueVectorPos, lists, move(prevOperator)},
      outDataChunkPos{outDataChunkPos} {
    outValueVector = make_shared<ValueVector>(lists->getDataType());
    outDataChunk = dataChunks->getDataChunk(outDataChunkPos);
    handle->setListSyncState(dataChunks->getListSyncState(outDataChunkPos));
    outDataChunk->append(outValueVector);
}

void RelPropertyListReader::getNextTuples() {
    prevOperator->getNextTuples();
    if (inDataChunk->numSelectedValues > 0) {
        readValuesFromList();
    }
    outValueVector->fillNullMask();
}

} // namespace processor
} // namespace graphflow
