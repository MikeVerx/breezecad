#pragma once
// «Проецировать геометрию» of what is not a model edge: work geometry and the
// origin, curves and points of another sketch, a sheet metal part's flat
// pattern, a model vertex. Plain geometry (no OCCT): each is a line, circle,
// arc, ellipse or point on a plane in space, projected square onto the
// sketch plane.

#include "core/Expected.h"
#include "core/Id.h"
#include "core/Math.h"
#include "kernel/Projection.h"

#include <optional>
#include <vector>

namespace bz::doc {
class Document;
struct SketchSource;
}

namespace bz::kernel {

struct SheetInfo;

/// What a projection puts into the sketch: a curve or a point.
struct ProjectedItem {
    std::optional<ProjectedCurve> curve;
    std::optional<Vec2>           point;
};

/// A curve on a plane in space (2D in `frame`): a line, a whole circle, an
/// arc counter-clockwise from `a` to `b`, or an ellipse (`a` and `b` the ends
/// of its major and minor axes).
struct PlanarCurve {
    enum class Kind : unsigned char { Line, Circle, Arc, Ellipse, Point } kind = Kind::Line;
    Vec2   a, b, centre;
    double radius = 0.0;
};

/// `curve` (on the plane of `from`) seen square on the plane of `to`. A
/// circle at a slant becomes an ellipse, seen edge-on a line; an arc or an
/// ellipse at a slant is an error (as for model edges), a line seen end-on
/// is a point.
Expected<ProjectedItem> ProjectPlanar(const Frame& from, const PlanarCurve& curve, const Frame& to);

/// A work plane, axis or point, or an origin element (doc::origin), on
/// `plane`: a plane gives the line where it crosses the sketch plane, an
/// axis a line (a point when it stands square to the sketch), a point a
/// point. Lines run `half` mm both ways from the point nearest the
/// element's own origin. Errors: not placed, parallel to the sketch.
Expected<ProjectedItem> ProjectWork(const doc::Document& doc, ObjectId work, const Frame& plane, double half);

/// The curve or point `source.curve` of the sketch `source.sketch` (in `doc`) on `plane`.
Expected<ProjectedItem> ProjectSketchEntity(const doc::Document& doc, const doc::SketchSource& source, const Frame& plane);

/// The curves of a sheet metal part's flat pattern, as Projection::part
/// counts them: the outline's segments (outer loop, then the holes), then
/// the bend lines. 2D in the sheet's frame.
std::vector<PlanarCurve> FlatPatternCurves(const SheetInfo& sheet);
/// How many of them are bend lines (the last ones).
std::size_t FlatPatternBends(const SheetInfo& sheet);

} // namespace bz::kernel
