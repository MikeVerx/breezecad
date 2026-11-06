#pragma once
// What is under the cursor in a sketch, and where a new point should snap.

#include "app/sketch/SketchView.h"
#include "core/Units.h"
#include "sketch/Sketch.h"

#include <optional>

namespace bz::app {

/// Sketch entities in a screen box: wholly inside (`window`) or touching it (SketchEditor.cpp).
std::vector<ObjectId> EntitiesInBox(const sketch::Sketch& sk, const SketchView& view, ImVec2 lo, ImVec2 hi, bool window);

/// Entity under the cursor. Points win over curves, curves over dimensions.
struct SketchHit {
    ObjectId           id;
    sketch::EntityKind kind = sketch::EntityKind::None;
    explicit operator bool() const { return id.IsValid(); }
};

/// The pick radii are for a mouse or the phone's cursor tip (a few dp); a bare
/// finger needs ~3 mm. The phone sets this every frame (1 = as on the desktop).
void SetPickScale(float scale);

/// Closest entity within a few pixels of `mouse`. Dimension labels count as
/// their constraint. `ignore` skips one point (the one being dragged).
SketchHit HitTest(const sketch::Sketch& sk, const SketchView& view, ImVec2 mouse, const UnitSystem& units,
                  ObjectId ignore = {});

enum class SnapKind : unsigned char { None, Point, Midpoint, Curve, Grid, Intersection };

struct Snap {
    Vec2     pos;
    SnapKind kind = SnapKind::None;
    ObjectId point;          ///< SnapKind::Point: existing point to reuse
    ObjectId curve;          ///< SnapKind::Curve: curve the point lies on; Midpoint: the line or arc;
                             ///< Intersection: the first of the two curves
    ObjectId curve2;         ///< SnapKind::Intersection: the second curve
    /// Polar tracking held the direction from `from` at this angle (degrees,
    /// 0…360, counter-clockwise from +X), for the guide and its label.
    std::optional<double> polarAngle;
    std::optional<Vec2>   polarFrom;   ///< where the angle is measured from
    bool     horizontal = false;   ///< aligned horizontally with `from`
    bool     vertical   = false;
    /// Lined up with an existing point (same x / same y), for the dashed
    /// guide. Only the position is aligned; no constraint is made.
    std::optional<Vec2> alignedX, alignedY;
    /// The new line would be square to this line (it ends on it, or starts
    /// where it ends): the position is turned to exactly 90° within a few
    /// degrees, and the line gets a Perpendicular constraint.
    ObjectId perpendicularTo;
    /// The new line runs parallel to this line (a line the cursor passed
    /// over during the command, as in Inventor): Parallel constraint.
    ObjectId parallelTo;
    /// The new line leaves the end of this arc (or a point on this circle)
    /// along its tangent: Tangent constraint.
    ObjectId tangentTo;
};

struct SnapOptions {
    bool                points = true;
    bool                grid   = true;
    double              gridStep = 1.0;     ///< mm
    std::optional<Vec2> from;               ///< previous click, for H/V inference
    ObjectId            fromPoint;          ///< the existing point at `from`, if any (perpendicular to its lines)
    ObjectId            fromCurve;          ///< the line `from` lies on, if it was snapped onto one
    ObjectId            ignore;
    bool                ortho = false;      ///< "Орто": from `from` only horizontally or vertically
    double              polarStep = 0.0;    ///< polar tracking: from `from` at multiples of this angle (degrees), 0 off
    ObjectId            reference;          ///< a line the cursor rested on this command: parallel/square to it
};

/// Where a click at `mouse` lands, with the reason for the CAD feedback.
Snap SnapCursor(const sketch::Sketch& sk, const SketchView& view, ImVec2 mouse, const SnapOptions& options);

/// Closest point on a line/circle/arc to `p` (sketch coordinates).
Vec2 ClosestOnCurve(const sketch::Sketch& sk, ObjectId curve, Vec2 p);

/// Grid step (mm) so that grid lines are at least `minPixels` apart:
/// 1, 2, 5 x 10^n.
double AdaptiveGridStep(double mmPerPixel, double minPixels = 14.0);

} // namespace bz::app
