#pragma once
// What is under the cursor in a sketch, and where a new point should snap.

#include "app/sketch/SketchView.h"
#include "core/Units.h"
#include "sketch/Sketch.h"

#include <optional>

namespace bz::app {

/// Entity under the cursor. Points win over curves, curves over dimensions.
struct SketchHit {
    ObjectId           id;
    sketch::EntityKind kind = sketch::EntityKind::None;
    explicit operator bool() const { return id.IsValid(); }
};

/// Closest entity within a few pixels of `mouse`. Dimension labels count as
/// their constraint. `ignore` skips one point (the one being dragged).
SketchHit HitTest(const sketch::Sketch& sk, const SketchView& view, ImVec2 mouse, const UnitSystem& units,
                  ObjectId ignore = {});

enum class SnapKind : unsigned char { None, Point, Curve, Grid };

struct Snap {
    Vec2     pos;
    SnapKind kind = SnapKind::None;
    ObjectId point;          ///< SnapKind::Point: existing point to reuse
    ObjectId curve;          ///< SnapKind::Curve: curve the point lies on
    bool     horizontal = false;   ///< aligned horizontally with `from`
    bool     vertical   = false;
};

struct SnapOptions {
    bool                points = true;
    bool                grid   = true;
    double              gridStep = 1.0;     ///< mm
    std::optional<Vec2> from;               ///< previous click, for H/V inference
    ObjectId            ignore;
};

/// Where a click at `mouse` lands, with the reason for the CAD feedback.
Snap SnapCursor(const sketch::Sketch& sk, const SketchView& view, ImVec2 mouse, const SnapOptions& options);

/// Closest point on a line/circle/arc to `p` (sketch coordinates).
Vec2 ClosestOnCurve(const sketch::Sketch& sk, ObjectId curve, Vec2 p);

/// Grid step (mm) so that grid lines are at least `minPixels` apart:
/// 1, 2, 5 x 10^n.
double AdaptiveGridStep(double mmPerPixel, double minPixels = 14.0);

} // namespace bz::app
