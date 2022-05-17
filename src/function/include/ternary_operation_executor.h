#pragma once

#include <functional>

#include "src/common/include/type_utils.h"
#include "src/common/include/vector/value_vector.h"
#include "src/common/types/include/value.h"

namespace graphflow {
namespace function {

struct TernaryOperationWrapper {
    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename OP>
    static inline void operation(A_TYPE& a, B_TYPE& b, C_TYPE& c, bool isANull, bool isBNull,
        bool isCNull, RESULT_TYPE& result, void* dataptr) {
        OP::operation(a, b, c, result, isANull, isBNull, isCNull);
    }
};

struct TernaryStringAndListOperationWrapper {
    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename OP>
    static inline void operation(A_TYPE& a, B_TYPE& b, C_TYPE& c, bool isANull, bool isBNull,
        bool isCNull, RESULT_TYPE& result, void* dataptr) {
        OP::operation(a, b, c, result, isANull, isBNull, isCNull, *(ValueVector*)dataptr);
    }
};

struct TernaryOperationExecutor {
    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeOnValue(ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result,
        uint64_t aPos, uint64_t bPos, uint64_t cPos, uint64_t resPos) {
        auto aValues = (A_TYPE*)a.values;
        auto bValues = (B_TYPE*)b.values;
        auto cValues = (C_TYPE*)c.values;
        auto resValues = (RESULT_TYPE*)result.values;
        OP_WRAPPER::template operation<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC>(aValues[aPos],
            bValues[bPos], cValues[cPos], (bool)a.isNull(aPos), (bool)b.isNull(bPos),
            (bool)c.isNull(cPos), resValues[resPos], (void*)&result);
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeAllFlat(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        auto aPos = a.state->getPositionOfCurrIdx();
        auto bPos = b.state->getPositionOfCurrIdx();
        auto cPos = c.state->getPositionOfCurrIdx();
        auto resPos = result.state->getPositionOfCurrIdx();
        result.setNull(resPos, a.isNull(aPos) || b.isNull(bPos) || c.isNull(cPos));
        if (!result.isNull(resPos)) {
            executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                a, b, c, result, aPos, bPos, cPos, resPos);
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeFlatFlatUnflat(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        auto aPos = a.state->getPositionOfCurrIdx();
        auto bPos = b.state->getPositionOfCurrIdx();
        // c and result should share the same dataChunk state.
        assert(c.state == result.state);
        if (a.isNull(aPos) || b.isNull(bPos)) {
            result.setAllNull();
        } else if (c.hasNoNullsGuarantee()) {
            if (c.state->isUnfiltered()) {
                for (auto i = 0u; i < c.state->selectedSize; ++i) {
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, aPos, bPos, i, i);
                }
            } else {
                for (auto i = 0u; i < c.state->selectedSize; ++i) {
                    auto pos = c.state->selectedPositions[i];
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, aPos, bPos, pos, pos);
                }
            }
        } else {
            if (c.state->isUnfiltered()) {
                for (auto i = 0u; i < c.state->selectedSize; ++i) {
                    result.setNull(i, c.isNull(i));
                    if (!result.isNull(i)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, aPos, bPos, i, i);
                    }
                }
            } else {
                for (auto i = 0u; i < c.state->selectedSize; ++i) {
                    auto pos = c.state->selectedPositions[i];
                    result.setNull(pos, c.isNull(pos));
                    if (!result.isNull(pos)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, aPos, bPos, pos, pos);
                    }
                }
            }
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeFlatUnflatUnflat(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        auto aPos = a.state->getPositionOfCurrIdx();
        // b, c and result should share the same dataChunk state.
        assert(b.state == c.state && c.state == result.state);
        if (a.isNull(aPos)) {
            result.setAllNull();
        } else if (b.hasNoNullsGuarantee() && c.hasNoNullsGuarantee()) {
            if (b.state->isUnfiltered()) {
                for (auto i = 0u; i < b.state->selectedSize; ++i) {
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, aPos, i, i, i);
                }
            } else {
                for (auto i = 0u; i < b.state->selectedSize; ++i) {
                    auto pos = b.state->selectedPositions[i];
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, aPos, pos, pos, pos);
                }
            }
        } else {
            if (b.state->isUnfiltered()) {
                for (auto i = 0u; i < b.state->selectedSize; ++i) {
                    result.setNull(i, b.isNull(i) || c.isNull(i));
                    if (!result.isNull(i)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, aPos, i, i, i);
                    }
                }
            } else {
                for (auto i = 0u; i < b.state->selectedSize; ++i) {
                    auto pos = b.state->selectedPositions[i];
                    result.setNull(pos, b.isNull(pos) || c.isNull(pos));
                    if (!result.isNull(pos)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, aPos, pos, pos, pos);
                    }
                }
            }
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeFlatUnflatFlat(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        auto aPos = a.state->getPositionOfCurrIdx();
        auto cPos = c.state->getPositionOfCurrIdx();
        // b and result should share the same dataChunk state.
        assert(b.state == result.state);
        if (a.isNull(aPos) || c.isNull(cPos)) {
            result.setAllNull();
        } else if (b.hasNoNullsGuarantee()) {
            if (b.state->isUnfiltered()) {
                for (auto i = 0u; i < b.state->selectedSize; ++i) {
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, aPos, i, cPos, i);
                }
            } else {
                for (auto i = 0u; i < b.state->selectedSize; ++i) {
                    auto pos = b.state->selectedPositions[i];
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, aPos, pos, cPos, pos);
                }
            }
        } else {
            if (b.state->isUnfiltered()) {
                for (auto i = 0u; i < b.state->selectedSize; ++i) {
                    result.setNull(i, b.isNull(i));
                    if (!result.isNull(i)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, aPos, i, cPos, i);
                    }
                }
            } else {
                for (auto i = 0u; i < b.state->selectedSize; ++i) {
                    auto pos = b.state->selectedPositions[i];
                    result.setNull(pos, b.isNull(pos));
                    if (!result.isNull(pos)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, aPos, pos, cPos, pos);
                    }
                }
            }
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeAllUnFlat(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        // a, b, c and result should share the same dataChunk state.
        assert(a.state == b.state && b.state == c.state && c.state == result.state);
        if (a.hasNoNullsGuarantee() && b.hasNoNullsGuarantee() && c.hasNoNullsGuarantee()) {
            if (a.state->isUnfiltered()) {
                for (uint64_t i = 0; i < a.state->selectedSize; i++) {
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, i, i, i, i);
                }
            } else {
                for (uint64_t i = 0; i < a.state->selectedSize; i++) {
                    auto pos = a.state->selectedPositions[i];
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, pos, pos, pos, pos);
                }
            }
        } else {
            if (a.state->isUnfiltered()) {
                for (uint64_t i = 0; i < a.state->selectedSize; i++) {
                    result.setNull(i, a.isNull(i) || b.isNull(i) || c.isNull(i));
                    if (!result.isNull(i)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, i, i, i, i);
                    }
                }
            } else {
                for (uint64_t i = 0; i < a.state->selectedSize; i++) {
                    auto pos = a.state->selectedPositions[i];
                    result.setNull(pos, a.isNull(pos) || b.isNull(pos) || c.isNull(pos));
                    if (!result.isNull(pos)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, pos, pos, pos, pos);
                    }
                }
            }
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeUnflatFlatFlat(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        auto bPos = b.state->getPositionOfCurrIdx();
        auto cPos = c.state->getPositionOfCurrIdx();
        // a and result should share the same dataChunk state.
        assert(a.state == result.state);
        if (b.isNull(bPos) || c.isNull(cPos)) {
            result.setAllNull();
        } else if (a.hasNoNullsGuarantee()) {
            if (a.state->isUnfiltered()) {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, i, bPos, cPos, i);
                }
            } else {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    auto pos = a.state->selectedPositions[i];
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, pos, bPos, cPos, pos);
                }
            }
        } else {
            if (a.state->isUnfiltered()) {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    result.setNull(i, a.isNull(i));
                    if (!result.isNull(i)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, i, bPos, cPos, i);
                    }
                }
            } else {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    auto pos = a.state->selectedPositions[i];
                    result.setNull(pos, a.isNull(pos));
                    if (!result.isNull(pos)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, pos, bPos, cPos, pos);
                    }
                }
            }
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeUnflatFlatUnflat(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        auto bPos = b.state->getPositionOfCurrIdx();
        // a, c and result should share the same dataChunk state.
        assert(a.state == c.state && c.state == result.state);
        if (b.isNull(bPos)) {
            result.setAllNull();
        } else if (a.hasNoNullsGuarantee() && c.hasNoNullsGuarantee()) {
            if (a.state->isUnfiltered()) {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, i, bPos, i, i);
                }
            } else {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    auto pos = a.state->selectedPositions[i];
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, pos, bPos, pos, pos);
                }
            }
        } else {
            if (a.state->isUnfiltered()) {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    result.setNull(i, a.isNull(i) || c.isNull(i));
                    if (!result.isNull(i)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, i, bPos, i, i);
                    }
                }
            } else {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    auto pos = b.state->selectedPositions[i];
                    result.setNull(pos, a.isNull(pos) || c.isNull(pos));
                    if (!result.isNull(pos)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, pos, bPos, pos, pos);
                    }
                }
            }
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeUnflatUnFlatFlat(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        auto cPos = c.state->getPositionOfCurrIdx();
        // a, b and result should share the same dataChunk state.
        assert(a.state == b.state && b.state == result.state);
        if (c.isNull(cPos)) {
            result.setAllNull();
        } else if (a.hasNoNullsGuarantee() && b.hasNoNullsGuarantee()) {
            if (a.state->isUnfiltered()) {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, i, i, cPos, i);
                }
            } else {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    auto pos = a.state->selectedPositions[i];
                    executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                        a, b, c, result, pos, pos, cPos, pos);
                }
            }
        } else {
            if (a.state->isUnfiltered()) {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    result.setNull(i, a.isNull(i) || b.isNull(i));
                    if (!result.isNull(i)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, i, i, cPos, i);
                    }
                }
            } else {
                for (auto i = 0u; i < a.state->selectedSize; ++i) {
                    auto pos = a.state->selectedPositions[i];
                    result.setNull(pos, a.isNull(pos) || b.isNull(pos));
                    if (!result.isNull(pos)) {
                        executeOnValue<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                            a, b, c, result, pos, pos, cPos, pos);
                    }
                }
            }
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC,
        typename OP_WRAPPER>
    static void executeSwitch(ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        result.resetOverflowBuffer();
        if (a.state->isFlat() && b.state->isFlat() && c.state->isFlat()) {
            executeAllFlat<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(a, b, c, result);
        } else if (a.state->isFlat() && b.state->isFlat() && !c.state->isFlat()) {
            executeFlatFlatUnflat<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                a, b, c, result);
        } else if (a.state->isFlat() && !b.state->isFlat() && !c.state->isFlat()) {
            executeFlatUnflatUnflat<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                a, b, c, result);
        } else if (a.state->isFlat() && !b.state->isFlat() && c.state->isFlat()) {
            executeFlatUnflatFlat<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                a, b, c, result);
        } else if (!a.state->isFlat() && !b.state->isFlat() && !c.state->isFlat()) {
            executeAllUnFlat<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                a, b, c, result);
        } else if (!a.state->isFlat() && !b.state->isFlat() && c.state->isFlat()) {
            executeUnflatUnFlatFlat<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                a, b, c, result);
        } else if (!a.state->isFlat() && b.state->isFlat() && c.state->isFlat()) {
            executeUnflatFlatFlat<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                a, b, c, result);
        } else if (!a.state->isFlat() && b.state->isFlat() && !c.state->isFlat()) {
            executeUnflatFlatUnflat<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, OP_WRAPPER>(
                a, b, c, result);
        } else {
            assert(false);
        }
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC>
    static void execute(ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        executeSwitch<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC, TernaryOperationWrapper>(
            a, b, c, result);
    }

    template<typename A_TYPE, typename B_TYPE, typename C_TYPE, typename RESULT_TYPE, typename FUNC>
    static void executeStringAndList(
        ValueVector& a, ValueVector& b, ValueVector& c, ValueVector& result) {
        executeSwitch<A_TYPE, B_TYPE, C_TYPE, RESULT_TYPE, FUNC,
            TernaryStringAndListOperationWrapper>(a, b, c, result);
    }
};

} // namespace function
} // namespace graphflow
