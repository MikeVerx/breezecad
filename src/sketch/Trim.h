#pragma once
// "Обрезка" and "Удлинение". Обрезка removes the piece of a curve between its nearest crossings with
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

// --- "Удлинение" -----------------------------------------------------------

/// An end of a line moved out to the nearest curve ahead of it.
struct Extension {
    ObjectId line;
    ObjectId end;      ///< the point that moves
    Vec2     to;
    ObjectId target;   ///< the curve it stops on
};

/// The extension of the end of `line` nearer `pick`; nullopt when no curve
/// lies ahead or the end is shared with another curve (it would drag it).
std::optional<Extension> FindExtension(const Sketch& sk, ObjectId line, Vec2 pick);

/// Moves the end and ties it to the target (PointOnCurve).
void Extend(Sketch& sk, IdGenerator& ids, const Extension& extension);

// --- "Разбить" -------------------------------------------------------------

/// Where a click at `pick` would split `curve` (a line or an arc): the point
/// on it, nullopt at or near its ends or for other curves.
std::optional<Vec2> SplitPoint(const Sketch& sk, ObjectId curve, Vec2 pick);

/// Splits the line or arc there into two sharing a new point: the halves of
/// a line stay in one line (parallel through a common point), the halves of
/// an arc share its centre. A length on the line goes (it no longer fits).
/// Returns the new point.
std::optional<ObjectId> Split(Sketch& sk, IdGenerator& ids, ObjectId curve, Vec2 pick);

} // namespace bz::sketch
