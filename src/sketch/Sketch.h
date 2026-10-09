#pragma once

#include "core/Id.h"
#include "core/Math.h"
#include "core/Parameter.h"

#include <array>
#include <string>
#include <vector>

namespace bz::sketch {

// ---------------------------------------------------------------------------
// Geometry. Coordinates are in the sketch plane, millimetres.
// Curves reference points by id, so moving a point moves every curve that
// shares it, and coincidence of curve ends is just a shared point id or a
// Coincident constraint.
// ---------------------------------------------------------------------------

struct Point {
    ObjectId id;
    Vec2     pos;
    bool     isOrigin = false;   ///< the projected origin: fixed, not deletable
    bool     projected = false;  ///< belongs to projected model geometry: placed by the model, fixed for the solver
};

struct Line {
    ObjectId id;
    ObjectId start;
    ObjectId end;
    bool     construction = false;
    bool     centerline = false;  ///< осевая: an axis for revolve and coil; not part of profiles
    bool     projected = false;   ///< a model edge projected into the sketch (see doc::SketchFeature)
};

struct Circle {
    ObjectId id;
    ObjectId center;
    double   radius = 1.0;
    bool     construction = false;
    bool     projected = false;   ///< a model edge projected into the sketch: its radius is fixed too
};

/// Counter-clockwise arc from `start` to `end` around `center`.
/// The solver keeps |start - center| == |end - center|.
struct Arc {
    ObjectId id;
    ObjectId center;
    ObjectId start;
    ObjectId end;
    bool     construction = false;
    bool     projected = false;   ///< a model edge projected into the sketch: its radius is fixed too
};

/// A smooth curve through or along its points ("Сплайн"). The points are
/// ordinary sketch points: constraints and dragging move them, the curve
/// follows. Closed when the last point is the first one.
struct Spline {
    ObjectId              id;
    std::vector<ObjectId> points;
    bool                  interpolate = true;   ///< through the points; false: they are control vertices
    bool                  construction = false;

    bool Closed() const { return points.size() > 2 && points.front() == points.back(); }
};

/// An ellipse by three points: the centre and the ends of its two axes
/// (the solver keeps the axes perpendicular). Axis sizes and the turn are
/// ordinary dimensions between the points.
struct Ellipse {
    ObjectId id;
    ObjectId center;
    ObjectId major;   ///< end of one axis
    ObjectId minor;   ///< end of the other
    bool     construction = false;
};

/// Text in a box ("Текст", as Inventor's text box). The box is four
/// construction lines with their corners: ordinary sketch geometry, so it can
/// be dimensioned, tied to other geometry and turned; the text is laid out
/// inside it, along its bottom side. The letters are closed outlines: they
/// extrude as relief or cut as engraving.
struct Text {
    ObjectId                id;
    std::array<ObjectId, 4> corners{};   ///< bottom-left, bottom-right, top-right, top-left
    std::array<ObjectId, 4> sides{};     ///< bottom, right, top, left (construction lines, owned)
    std::string             text;        ///< UTF-8, lines separated by '\n'
    double                  height = 5.0;   ///< font size, mm
    std::string             font;        ///< family name; empty: the program's font
    bool                    bold = false;
    bool                    italic = false;
    unsigned char           hAlign = 0;  ///< 0 left, 1 centre, 2 right
    unsigned char           vAlign = 0;  ///< 0 bottom, 1 middle, 2 top
    bool                    construction = false;
};

// ---------------------------------------------------------------------------
// Constraints. Geometric ones have no value; dimensional ones carry a
// Parameter, which shows up in the variables table and may hold an
// expression ("w / 2").
// ---------------------------------------------------------------------------

enum class ConstraintType : unsigned char {
    // geometric
    Coincident,      ///< point, point
    Horizontal,      ///< line                (or point, point)
    Vertical,        ///< line                (or point, point)
    Parallel,        ///< line, line
    Perpendicular,   ///< line, line
    Equal,           ///< line, line  |  circle/arc, circle/arc
    Fix,             ///< point (held at `fixedAt`)
    PointOnCurve,    ///< point, line/circle/arc
    Midpoint,        ///< point, line
    Tangent,         ///< line, circle/arc  |  circle/arc, circle/arc
    Concentric,      ///< circle/arc, circle/arc
    Symmetric,       ///< point, point, line — mirror images across the line
    // dimensional
    Distance,        ///< point, point        — straight distance
    DistanceX,       ///< point, point        — horizontal distance
    DistanceY,       ///< point, point        — vertical distance
    Length,          ///< line
    PointLineDistance, ///< point, line
    Radius,          ///< circle/arc
    Diameter,        ///< circle/arc
    Angle,           ///< line, line          — counter-clockwise from first to second
};

bool IsDimensional(ConstraintType type);

struct Constraint {
    ObjectId                id;
    ConstraintType          type = ConstraintType::Coincident;
    std::array<ObjectId, 3> refs{};          ///< entities, see ConstraintType (the third: Symmetric's line)

    bool References(ObjectId entity) const {
        return entity && (refs[0] == entity || refs[1] == entity || refs[2] == entity);
    }
    Parameter               parameter;       ///< dimensional constraints only
    Vec2                    fixedAt;         ///< Fix only
    Vec2                    labelOffset;     ///< where the dimension text sits, relative to its anchor
    /// Reference (driven) dimension: shows a measured value, constrains nothing.
    bool                    reference = false;
};

/// Kind of an entity found by id.
enum class EntityKind : unsigned char { None, Point, Line, Circle, Arc, Constraint, Spline, Text, Ellipse };

/// A complete 2D sketch: geometry plus constraints.
class Sketch {
public:
    std::vector<Point>      points;
    std::vector<Line>       lines;
    std::vector<Circle>     circles;
    std::vector<Arc>        arcs;
    std::vector<Spline>     splines;
    std::vector<Text>       texts;
    std::vector<Ellipse>    ellipses;
    std::vector<Constraint> constraints;

    // --- creation (ids come from the document's generator) -------------
    ObjectId AddPoint(IdGenerator& ids, Vec2 pos);
    ObjectId AddLine(IdGenerator& ids, ObjectId start, ObjectId end, bool construction = false);
    ObjectId AddCircle(IdGenerator& ids, ObjectId center, double radius, bool construction = false);
    ObjectId AddArc(IdGenerator& ids, ObjectId center, ObjectId start, ObjectId end, bool construction = false);
    /// A text with its box from `bottomLeft` to `topRight` (axis-aligned; the
    /// box's constraints keep it a rectangle with a horizontal bottom).
    ObjectId AddText(IdGenerator& ids, Vec2 bottomLeft, Vec2 topRight, std::string text, double height);
    ObjectId AddEllipse(IdGenerator& ids, ObjectId center, ObjectId major, ObjectId minor, bool construction = false);
    ObjectId AddSpline(IdGenerator& ids, std::vector<ObjectId> points, bool interpolate, bool construction = false);
    ObjectId AddConstraint(IdGenerator& ids, Constraint constraint);
    /// Creates the fixed origin point if the sketch has none. Returns its id.
    ObjectId EnsureOrigin(IdGenerator& ids);

    // --- lookup ---------------------------------------------------------
    EntityKind KindOf(ObjectId id) const;
    Point*            FindPoint(ObjectId id);
    const Point*      FindPoint(ObjectId id) const;
    const Line*       FindLine(ObjectId id) const;
    Line*             FindLine(ObjectId id);
    const Circle*     FindCircle(ObjectId id) const;
    Circle*           FindCircle(ObjectId id);
    const Arc*        FindArc(ObjectId id) const;
    const Spline*     FindSpline(ObjectId id) const;
    const Ellipse*    FindEllipse(ObjectId id) const;
    Text*             FindText(ObjectId id);
    const Text*       FindText(ObjectId id) const;
    const Constraint* FindConstraint(ObjectId id) const;
    Constraint*       FindConstraint(ObjectId id);
    const Point*      Origin() const;

    /// Position of a point; {0,0} when the id is unknown.
    Vec2 PositionOf(ObjectId pointId) const;
    /// Radius of a circle or arc; 0 when the id is unknown.
    double RadiusOf(ObjectId curveId) const;

    // --- editing --------------------------------------------------------
    /// Removes an entity and everything that depends on it: a point takes
    /// its curves along, a curve takes its constraints. Points left without
    /// any curve are removed too. The origin is never removed.
    void Remove(ObjectId id);

    /// Merges point `from` into point `into`: curves and constraints that
    /// referenced `from` now reference `into`. Used when a new point snaps
    /// onto an existing one.
    void MergePoints(ObjectId from, ObjectId into);

    bool IsEmpty() const { return lines.empty() && circles.empty() && arcs.empty() && splines.empty() && texts.empty() && ellipses.empty(); }

    /// Box around all curves and points (empty for an empty sketch).
    /// Circles count with their full extent; arcs too, which is a bit generous.
    Box2 Extents() const;

    /// Scales the whole sketch about `center`: points, radii, fixed positions
    /// and dimension label offsets. Dimension values are not touched.
    void ScaleAbout(Vec2 center, double factor);

private:
    bool IsPointUsed(ObjectId pointId) const;
    void RemoveConstraintsReferencing(ObjectId id);
};

/// A distance from a point to an осевая: shown and driven as a diameter
/// (twice the distance), as for a shaft profile drawn as a half.
bool IsDiametric(const Sketch& sk, const Constraint& c);

} // namespace bz::sketch
