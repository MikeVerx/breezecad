#pragma once
// "Обрезка": removes the piece of a curve between its nearest crossings with
// other curves, around the place that was clicked. A line or an arc keeps
// the rest (split in two when the piece was in the middle); a circle turns
// into the arc that remains. New ends sit on the curves that cut them.

#include "sketch/Sketch.h"

#include <optional>
#include <vector>

namespace bz::sketch {

/// Where a curve is cut: a parameter along it (line: 0..1 from start to
/// end; circle: angle; arc: angle from its start) and what cuts it there.
struct Cut {
    double   at = 0;
    Vec2     pos;
    ObjectId by;      ///< the cutting curve
    ObjectId point;   ///< an existing point already at the crossing, if any
};

/// The piece a click at `pick` would remove.
struct TrimSpan {
    ObjectId           curve;
    std::optional<Cut> from, to;   ///< bounding cuts; none on a side that runs to the curve's end
};

/// Crossings of `curve` with every other curve of the sketch, along it.
std::vector<Cut> CutsOf(const Sketch& sk, ObjectId curve);

/// The piece of `curve` around `pick`; nullopt for an unknown curve.
std::optional<TrimSpan> FindTrimSpan(const Sketch& sk, ObjectId curve, Vec2 pick);

/// Points along a span, for drawing it.
std::vector<Vec2> SpanPolyline(const Sketch& sk, const TrimSpan& span, int segments = 48);

/// Removes the span. Returns false when there is nothing to trim (a circle
/// cut fewer than two times is removed whole, which is still a trim).
bool Trim(Sketch& sk, IdGenerator& ids, const TrimSpan& span);

} // namespace bz::sketch
