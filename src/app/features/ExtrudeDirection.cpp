#include "app/features/ExtrudeDirection.h"

namespace bz::app {

bool ExtrudeAutoDirection::OnOperation(doc::BooleanOp before, doc::BooleanOp after, bool reversed, int materialSide) {
    using doc::BooleanOp;
    if (myUserChose || before == after) return reversed;
    if (after == BooleanOp::Cut) {
        // Into the material: against the normal when the body is behind the sketch.
        if (materialSide == 0) return reversed;
        const bool inward = materialSide < 0;
        if (inward != reversed) myTurned = true;
        return inward;
    }
    if (before == BooleanOp::Cut && myTurned) {
        myTurned = false;
        return !reversed;
    }
    return reversed;
}

} // namespace bz::app
