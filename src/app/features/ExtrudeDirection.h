#pragma once
// Which way an extrusion goes when its operation changes: a «Вырез» turns
// into the body by itself, and back again when the operation is no longer
// a cut — unless the user has chosen the direction himself. Free of the UI
// so that the tests can use it.

#include "document/SketchSolidFeature.h"

namespace bz::app {

class ExtrudeAutoDirection {
public:
    /// The operation went from `before` to `after`; `reversed` is the
    /// direction now, `materialSide` where the body is from the sketch
    /// (+1 along its normal, -1 against it, 0 both ways or nowhere:
    /// Model::MaterialSide). Returns the direction to use.
    bool OnOperation(doc::BooleanOp before, doc::BooleanOp after, bool reversed, int materialSide);
    /// The user turned the direction himself: leave it alone from now on.
    void UserChanged() { myUserChose = true; }
    /// A new operation, or another one opened for editing.
    void Reset() { *this = {}; }

private:
    bool myUserChose = false;
    bool myTurned = false;   ///< turned into the body by OnOperation, not yet back
};

} // namespace bz::app
