#pragma once
// "Эквидистанта" in a sketch: a copy of a chain of curves (lines and arcs
// joined end to end, or a single circle) at a distance to one side. Lines
// of the copy are parallel to their originals, arcs and circles concentric;
// neighbours share their joints, found where the offset curves cross.

#include "sketch/Sketch.h"

#include <optional>
#include <utility>
#include <vector>

namespace bz::sketch {

/// Curves joined end to end, in order of travel.
struct Chain {
    struct Piece {
        ObjectId curve;
        bool     reversed = false;   ///< travelled end to start (a line) or clockwise (an arc)
    };
    std::vector<Piece> pieces;
    bool               closed = false;
};

/// The chain through `curve`: every line and arc reachable through ends
/// shared by exactly two curves; a circle on its own. Projections and
/// unknown ids give nullopt.
std::optional<Chain> ChainOf(const Sketch& sk, ObjectId curve);

/// The side of the chain `p` is on: +1 left of the travel direction, -1 right
/// (judged by the nearest piece).
int SideOf(const Sketch& sk, const Chain& chain, Vec2 p);

/// The offset as polylines, for previewing; empty when it cannot be built.
/// `distance` is signed: positive to the left of the travel direction; 0
/// gives the chain itself.
std::vector<std::vector<Vec2>> OffsetPreview(const Sketch& sk, const Chain& chain, double distance);

struct OffsetCurves {
    std::vector<ObjectId> curves;   ///< the new curves, in chain order
    /// For distance dimensions: a point of an original line and the new
    /// line parallel to it.
    std::vector<std::pair<ObjectId, ObjectId>> distances;
};

/// Adds the offset copy. nullopt when it cannot be built: an arc would
/// shrink to nothing, or neighbours of the copy no longer meet.
std::optional<OffsetCurves> OffsetChain(Sketch& sk, IdGenerator& ids, const Chain& chain, double distance);

} // namespace bz::sketch
