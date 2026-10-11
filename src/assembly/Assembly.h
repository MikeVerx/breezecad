#pragma once
// An assembly: parts placed relative to each other and the constraints
// ("зависимости") that hold them. Parts are not copied in: a component
// refers to its part file by a path relative to the assembly file.
//
// No OCCT here. The geometry a constraint ties (a face, an axis...) is kept
// as numbers in the part's own coordinates; the kernel fills them in when
// the user picks and refreshes them when the part changes.

#include "assembly/Placement.h"
#include "core/Id.h"
#include "core/Math.h"

#include <string>
#include <string_view>
#include <vector>

namespace bz::assembly {

/// What a parts list (a future module) needs from a component.
struct ComponentProperties {
    std::string designation;   ///< "БРЗ.001.002"; empty: taken from the part
    std::string name;          ///< "Кронштейн"; empty: taken from the part
    std::string material;      ///< empty: taken from the part
    int         quantity = 1;  ///< how many this entry counts for (a parts list may group equal ones)

    bool operator==(const ComponentProperties&) const = default;
};

struct Component {
    ObjectId            id;
    std::string         name;       ///< in the tree: "Кронштейн:1"
    std::string         path;       ///< the part file, relative to the assembly, '/' separators
    Placement           placement;
    ComponentProperties properties;
    bool                grounded = false;   ///< "Закреплён": never moved by constraints or dragging
    bool                visible  = true;
};

/// Geometry of a part that a constraint ties, in the part's coordinates.
enum class GeometryKind : unsigned char {
    Plane,      ///< point on it + normal (outward for a face)
    Axis,       ///< point on it + direction (a cylinder's or cone's axis, a work axis)
    Point,      ///< a vertex, a centre
    Cylinder,   ///< axis + radius
    Sphere,     ///< centre + radius
    Circle,     ///< a circular edge: centre + axis (the axis of "Вставка", the plane through the centre)
};

struct GeometryRef {
    ObjectId     component;
    GeometryKind kind = GeometryKind::Plane;
    Vec3         origin;           ///< point (plane, axis, circle centre...)
    Vec3         direction;        ///< normal or axis, unit; zero for a point
    double       radius = 0.0;     ///< cylinder, sphere, circle
    Vec3         pick;             ///< where it was picked (to find it again after the part changes)

    bool IsSet() const { return component.IsValid(); }
    bool HasAxis() const {
        return kind == GeometryKind::Axis || kind == GeometryKind::Cylinder || kind == GeometryKind::Circle;
    }
};

enum class ConstraintType : unsigned char {
    Mate,        ///< "Совмещение": two planes face to face (or flush), with a gap
    Insert,      ///< "Вставка": axes coincide and the planes through two circles meet
    Angle,       ///< "Угол" between two planes or axes
    Tangent,     ///< "Касание": a cylinder or a sphere touches a plane, cylinder or sphere
    AxisMate,    ///< "Совпадение осей": two axes on one line
};

enum class ConstraintState : unsigned char { Unknown, Satisfied, Failed, Redundant };

struct Constraint {
    ObjectId       id;
    std::string    name;            ///< "Совмещение:1"
    ConstraintType type = ConstraintType::Mate;
    GeometryRef    first, second;
    double         value   = 0.0;   ///< gap / offset in mm; angle in radians
    bool           flipped = false; ///< Mate: flush (normals the same way); Insert: aligned; Tangent: inside
    bool           suppressed = false;

    // Computed by the solver, not saved.
    ConstraintState state = ConstraintState::Unknown;
    double          error = 0.0;    ///< how far from satisfied, mm or rad
};

/// Remaining freedom of a component, as the tree shows it.
struct Freedom {
    int translations = 3;   ///< 0..3
    int rotations    = 3;   ///< 0..3
    int Total() const { return translations + rotations; }
};

struct AssemblyInfo {
    std::string name;
    std::string designation;
};

class Assembly {
public:
    AssemblyInfo            info;
    std::vector<Component>  components;
    std::vector<Constraint> constraints;

    IdGenerator&       Ids() { return myIds; }
    const IdGenerator& Ids() const { return myIds; }

    Component*       FindComponent(ObjectId id);
    const Component* FindComponent(ObjectId id) const;
    int              ComponentIndex(ObjectId id) const;
    Constraint*       FindConstraint(ObjectId id);
    const Constraint* FindConstraint(ObjectId id) const;

    /// Adds a component with a fresh id and a tree name "<base>:N".
    Component& AddComponent(std::string path, std::string_view baseName, const Placement& at = {});
    /// Removes it and every constraint on it.
    void RemoveComponent(ObjectId id);
    /// Adds a constraint with a fresh id and a name "<type>:N".
    Constraint& AddConstraint(Constraint c);
    void RemoveConstraint(ObjectId id);

    /// Constraints that tie component `id`.
    std::vector<const Constraint*> ConstraintsOf(ObjectId id) const;

    /// Bumped on every change (redraw, autosave).
    std::uint64_t Revision() const { return myRevision; }
    void          Touch() { ++myRevision; }

private:
    IdGenerator   myIds;
    std::uint64_t myRevision = 0;
};

/// "Совмещение", "Вставка"... (Russian; the UI translates).
std::string_view ConstraintTitle(ConstraintType type);

} // namespace bz::assembly
