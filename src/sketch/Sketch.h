#pragma once

#include "core/Id.h"
#include "core/Math.h"
#include "core/Parameter.h"

#include <array>
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
enum class EntityKind : unsigned char { None, Point, Line, Circle, Arc, Constraint };

/// A complete 2D sketch: geometry plus constraints.
class Sketch {
public:
    std::vector<Point>      points;
    std::vector<Line>       lines;
    std::vector<Circle>     circles;
    std::vector<Arc>        arcs;
    std::vector<Constraint> constraints;

    // --- creation (ids come from the document's generator) -------------
    ObjectId AddPoint(IdGenerator& ids, Vec2 pos);
    ObjectId AddLine(IdGenerator& ids, ObjectId start, ObjectId end, bool construction = false);
    ObjectId AddCircle(IdGenerator& ids, ObjectId center, double radius, bool construction = false);
    ObjectId AddArc(IdGenerator& ids, ObjectId center, ObjectId start, ObjectId end, bool construction = false);
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

    bool IsEmpty() const { return lines.empty() && circles.empty() && arcs.empty(); }

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

} // namespace bz::sketch
