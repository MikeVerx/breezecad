#pragma once
// Moving and rotating part of a sketch as a whole: the points of the chosen
// curves go along, fixed points keep their new place. Rotation turns the
// horizontal / vertical constraints among the moved lines into parallel /
// perpendicular ones, so a rotated rectangle stays a rectangle.

#include "sketch/Sketch.h"

#include <span>
#include <vector>

namespace bz::sketch {

/// A rigid motion: rotate by `angle` about `centre`, then shift by `offset`.
struct Motion {
    Vec2   centre;
    double angle = 0;   ///< radians, counter-clockwise
    Vec2   offset;

    Vec2 Apply(Vec2 p) const;
};

/// Points that move with `entities` (points, curves; constraints are ignored).
/// The origin and projected points never move.
std::vector<ObjectId> MovedPoints(const Sketch& sk, std::span<const ObjectId> entities);

/// Moves `entities`. Returns false when nothing could move.
bool MoveEntities(Sketch& sk, IdGenerator& ids, std::span<const ObjectId> entities, const Motion& motion);

/// Copies the curves among `entities` with their own points, placed by
/// `motion`. Shape constraints among the copied curves come along (sizes
/// too, as equalities to the originals), positions do not. Returns the new curves.
std::vector<ObjectId> CopyEntities(Sketch& sk, IdGenerator& ids, std::span<const ObjectId> entities, const Motion& motion);

/// Scales the points of `entities` about `centre` by `factor` (radii too).
bool ScaleEntities(Sketch& sk, std::span<const ObjectId> entities, Vec2 centre, double factor);

} // namespace bz::sketch
