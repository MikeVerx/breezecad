#pragma once
// A model edge as it appears on a sketch plane ("Проецировать геометрию").
// OCCT-free: the sketch editor makes sketch curves from it.

#include "core/Id.h"
#include "core/Math.h"
#include "sketch/Sketch.h"

#include <optional>
#include <vector>

namespace bz::kernel {

struct ProjectedCurve {
    enum class Kind : unsigned char { Line, Circle, Arc, Ellipse } kind = Kind::Line;
    Vec2   start, end;      ///< line ends; arc ends, counter-clockwise on the sketch plane;
                            ///< ellipse: the ends of its major and minor axes
    Vec2   centre;          ///< circles, arcs and ellipses
    double radius = 0.0;
};

/// A body of revolution as it appears on a sketch plane: its axis, its
/// outline (the extreme positions of the profile) and where the plane cuts it.
struct RevolvedProjection {
    std::optional<ProjectedCurve> axis;     ///< a line, when the axis is not seen end-on
    std::optional<Vec2>           centre;   ///< the axis seen end-on
    std::vector<ProjectedCurve>   outline;
    std::vector<ProjectedCurve>   section;  ///< empty when the plane does not cut the body
    bool                          outlineSkipped = false;   ///< axis slanted to the plane: no outline
};

/// Moves the sketch curve `curve` (and its points) to `projected`. False when
/// the sketch has no such curve of that kind.
bool PlaceProjectedCurve(sketch::Sketch& sk, ObjectId curve, const ProjectedCurve& projected);
/// The sketch curve `curve` is where `projected` says (within `tolerance`, mm).
bool CurveMatches(const sketch::Sketch& sk, ObjectId curve, const ProjectedCurve& projected, double tolerance);

} // namespace bz::kernel
