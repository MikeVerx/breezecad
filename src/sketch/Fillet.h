#pragma once
// "Скругление" in a sketch: the corner where two lines meet becomes an arc of
// a given radius, tangent to both. The lines are shortened to the tangent
// points, which are the arc's ends. The corner point stays as a virtual
// sharp with dashed phantom sides leading to it, so dimensions and
// constraints on it keep holding and nothing else moves.

#include "sketch/Sketch.h"

#include <optional>
#include <utility>

namespace bz::sketch {

/// The two lines meeting at point `corner`; nullopt unless exactly two
/// lines end there and neither is a projection. Helper (construction) lines
/// at the corner, such as a centred rectangle's diagonals, are not counted
/// when outline lines meet there.
std::optional<std::pair<ObjectId, ObjectId>> CornerLines(const Sketch& sk, ObjectId corner);

/// Where the arc would go.
struct FilletPlan {
    ObjectId a, b;      ///< the lines
    Vec2     centre;
    Vec2     onA, onB;  ///< tangent points
};

/// The fillet of `corner` with `radius`; nullopt when it does not fit (the
/// lines are too short or in one line) or the corner is not two lines.
std::optional<FilletPlan> PlanFillet(const Sketch& sk, ObjectId corner, double radius);

/// Rounds the corner. Returns the new arc; nullopt when PlanFillet fails.
std::optional<ObjectId> FilletCorner(Sketch& sk, IdGenerator& ids, ObjectId corner, double radius);

} // namespace bz::sketch
