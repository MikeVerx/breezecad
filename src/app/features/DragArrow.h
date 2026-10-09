#pragma once
// A length arrow over the model that the user drags by its round tip
// (Выдавливание, Отверстие, Плоскость). It is drawn on top of everything so
// it can be grabbed even inside a body. The arrow only reports the dragged
// length; the operation decides what that length means.

#include "app/AppContext.h"

#include <optional>
#include <string>

namespace bz::app {

/// A round step for dragged values: about 4 pixels at this zoom (1, 2, 5 × 10^n mm).
double DragStep(double mmPerPixel);

class DragArrow {
public:
    /// Draws the arrow from `base` to `base + dir * length` (`dir` a unit
    /// vector) and handles the drag. `label` is shown next to the tip while
    /// dragging. Returns the new length, rounded to a step of the view, on
    /// the frames it changed; it may be negative (dragged past the base).
    std::optional<double> Update(AppContext& app, const ViewportFrame& vf, Vec3 base, Vec3 dir, double length,
                                 const std::string& label);

    /// The caller turned the arrow around (negative length): keep the grab
    /// point under the cursor.
    void Flipped() {
        myOffset = -myOffset;
        myLast = -myLast;
    }
    /// Stops a drag, e.g. when the arrow can no longer be shown.
    void Release() { myDragging = false; }

    bool Dragging() const { return myDragging; }
    /// The tip is under the cursor or held: a click belongs to the arrow.
    bool Hot() const { return myHot; }

private:
    bool   myDragging = false;
    bool   myHot = false;
    double myOffset = 0.0;   ///< tip position minus the grabbed point along the arrow
    double myLast = 0.0;     ///< last reported length, so unchanged frames report nothing
};

} // namespace bz::app
