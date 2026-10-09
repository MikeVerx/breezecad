#pragma once
// "Зеркальное отражение" in a sketch: copies of chosen curves reflected
// across a line, each point tied to its original by a Symmetric constraint
// (a point on the line stays one point), so the copy follows later edits.

#include "sketch/Sketch.h"

#include <span>
#include <vector>

namespace bz::sketch {

/// Reflects `p` across the line through `a` and `b`.
Vec2 Reflect(Vec2 p, Vec2 a, Vec2 b);

/// Mirrors the curves among `entities` across line `axis`. Returns the new
/// curves; empty when nothing could be mirrored (no curves, axis unknown or
/// among the entities).
std::vector<ObjectId> MirrorEntities(Sketch& sk, IdGenerator& ids, std::span<const ObjectId> entities, ObjectId axis);

} // namespace bz::sketch
