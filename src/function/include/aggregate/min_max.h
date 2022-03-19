#pragma once

#include "src/function/comparison/operations/include/comparison_operations.h"
#include "src/function/include/aggregate/aggregate_function.h"

using namespace graphflow::function::operation;

namespace graphflow {
namespace function {

template<typename T>
struct MinMaxFunction {

    struct MinMaxState : public AggregateState {
        inline uint64_t getStateSize() const override { return sizeof(*this); }
        inline uint8_t* getResult() const override { return (uint8_t*)&val; }

        T val;
    };

    static unique_ptr<AggregateState> initialize() { return make_unique<MinMaxState>(); }

    template<class OP>
    static void update(uint8_t* state_, ValueVector* input, uint64_t count) {
        auto state = reinterpret_cast<MinMaxState*>(state_);
        if (input->state->isFlat()) {
            auto pos = input->state->getPositionOfCurrIdx();
            if (!input->isNull(pos)) {
                updateSingleValue<OP>(state, input, pos);
            }
        } else {
            if (input->hasNoNullsGuarantee()) {
                for (auto i = 0u; i < input->state->selectedSize; ++i) {
                    auto pos = input->state->selectedPositions[i];
                    updateSingleValue<OP>(state, input, pos);
                }
            } else {
                for (auto i = 0u; i < input->state->selectedSize; ++i) {
                    auto pos = input->state->selectedPositions[i];
                    if (!input->isNull(pos)) {
                        updateSingleValue<OP>(state, input, pos);
                    }
                }
            }
        }
    }

    template<class OP>
    static void updateSingleValue(MinMaxState* state, ValueVector* input, uint32_t pos) {
        auto inputValues = (T*)input->values;
        if (state->isNull) {
            state->val = inputValues[pos];
            state->isNull = false;
        } else {
            uint8_t compare_result;
            OP::template operation(inputValues[pos], state->val, compare_result,
                false /* isLeftNull */, false /* isRightNull */);
            state->val = compare_result ? inputValues[pos] : state->val;
        }
    }

    template<class OP>
    static void combine(uint8_t* state_, uint8_t* otherState_) {
        auto otherState = reinterpret_cast<MinMaxState*>(otherState_);
        if (otherState->isNull) {
            return;
        }
        auto state = reinterpret_cast<MinMaxState*>(state_);
        if (state->isNull) {
            state->val = otherState->val;
            state->isNull = false;
        } else {
            uint8_t compareResult;
            OP::template operation(otherState->val, state->val, compareResult,
                false /* isLeftNull */, false /* isRightNull */);
            state->val = compareResult == 1 ? otherState->val : state->val;
        }
    }

    static void finalize(uint8_t* state_) {}
};

} // namespace function
} // namespace graphflow
