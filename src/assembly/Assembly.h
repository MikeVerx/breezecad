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
#include "document/Appearance.h"

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
    /// Joints: the frame's X, across `direction` (part coordinates). Zero:
    /// one made from `direction` (see JointFrameX).
    Vec3         reference{};

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

    // Joints ("соединения"): two frames (origin, Z = direction, X =
    // reference) tied so that only the motion the joint names is left.
    // Z of the frames face each other unless `flipped`.
    Rotational,  ///< "Вращение": one axis, origins' planes `value` apart; turns about Z
    Slider,      ///< "Ползун": one line, X kept; slides along Z
    Cylindrical, ///< "Цилиндрическое": one axis; turns and slides
    Planar,      ///< "Плоское": Z opposed, planes `value` apart; slides in the plane, turns about Z
    Ball,        ///< "Шаровое": origins coincide; turns any way
    Rigid,       ///< "Жёсткое": frames coincide; nothing left
};

/// A joint (Rotational…Rigid), not a plain constraint.
constexpr bool IsJoint(ConstraintType t) { return t >= ConstraintType::Rotational; }

/// What the one free coordinate of a joint measures.
enum class JointMotion : unsigned char {
    None,       ///< several or none (Cylindrical, Planar, Ball, Rigid)
    Rotation,   ///< Rotational: angle about Z, rad
    Slide,      ///< Slider: shift along Z, mm
};
constexpr JointMotion MotionOf(ConstraintType t) {
    return t == ConstraintType::Rotational ? JointMotion::Rotation
         : t == ConstraintType::Slider     ? JointMotion::Slide
                                           : JointMotion::None;
}

/// Where a joint's coordinate may go (rad or mm). An angle range is taken
/// round from `lower` to `upper`.
struct JointLimits {
    bool   enabled = false;
    double lower = 0.0, upper = 0.0;
    bool operator==(const JointLimits&) const = default;
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
    /// Joints with one coordinate: held at `position` (rad or mm, measured
    /// from where the frames' X agree / the origins' planes meet).
    bool           locked   = false;
    double         position = 0.0;
    JointLimits    limits;

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

/// How a component pattern ("Массив") repeats its seed.
enum class PatternKind : unsigned char {
    Holes,         ///< into the holes like the one the seed sits in (same size, plane and side of a part)
    Rectangular,   ///< rows and columns along two directions
    Circular,      ///< around an axis
};

/// A component pattern: copies ("instances") of a group of components (the
/// seed: a bolt with its washer and nut...) placed by a rule, each tied by
/// constraints like the seed's. One node in the tree; edited and removed as
/// a whole. The instances are ordinary components the kernel rebuilds
/// (kernel/ComponentPattern.h) when the pattern changes.
struct ComponentPattern {
    ObjectId              id;
    std::string           name;   ///< "Массив:1"
    PatternKind           kind = PatternKind::Holes;
    std::vector<ObjectId> seed;   ///< the components repeated, the main one first

    /// Holes: the rim of the hole the seed sits in (a Circle of the holder).
    GeometryRef rim;
    /// Rectangular: unit directions in the assembly, counts (with the seed) and steps, mm.
    Vec3   direction1{1, 0, 0}, direction2{0, 1, 0};
    int    count1 = 2, count2 = 1;
    double spacing1 = 20.0, spacing2 = 20.0;
    /// Circular: an axis of a component (Axis, Cylinder or Circle); unset:
    /// `axisOrigin`/`axisDirection` in the assembly. `angle` is the total
    /// sweep (2π: evenly all round), `count` includes the seed.
    GeometryRef axis;
    Vec3        axisOrigin, axisDirection{0, 0, 1};
    int         count = 4;
    double      angle = 2 * kPi;

    /// Per instance, the components made for it, in the order of `seed`.
    std::vector<std::vector<ObjectId>> instances;

    bool Contains(ObjectId component) const;   ///< in the seed or an instance
    bool IsInstance(ObjectId component) const;
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
    std::vector<ComponentPattern> patterns;
    /// Appearances set in this assembly over its parts' own (as Inventor's
    /// appearance overrides): body = component id; faces in the part's own
    /// coordinates, so a look follows its component. The parts' files are
    /// never changed by them. Pictures come with it (saved inside the file).
    doc::Look look;

    IdGenerator&       Ids() { return myIds; }
    const IdGenerator& Ids() const { return myIds; }

    Component*       FindComponent(ObjectId id);
    const Component* FindComponent(ObjectId id) const;
    int              ComponentIndex(ObjectId id) const;
    Constraint*       FindConstraint(ObjectId id);
    const Constraint* FindConstraint(ObjectId id) const;

    /// Adds a component with a fresh id and a tree name "<base>:N".
    Component& AddComponent(std::string path, std::string_view baseName, const Placement& at = {});
    /// Removes it and every constraint on it. Removing a pattern's seed
    /// component removes the pattern with its instances; removing an
    /// instance drops it from its pattern.
    void RemoveComponent(ObjectId id);
    /// Adds a constraint with a fresh id and a name "<type>:N".
    Constraint& AddConstraint(Constraint c);
    void RemoveConstraint(ObjectId id);

    /// Adds a pattern with a fresh id and a name "Массив:N" (no instances yet).
    ComponentPattern&       AddPattern(ComponentPattern p);
    ComponentPattern*       FindPattern(ObjectId id);
    const ComponentPattern* FindPattern(ObjectId id) const;
    /// The pattern `component` is an instance of, or nullptr.
    const ComponentPattern* PatternOfInstance(ObjectId component) const;
    /// Removes the instances of pattern `id` (components and their constraints).
    void ClearInstances(ObjectId id);
    /// Removes the pattern with its instances; the seed stays.
    void RemovePattern(ObjectId id);

    /// Constraints that tie component `id`.
    std::vector<const Constraint*> ConstraintsOf(ObjectId id) const;

    /// Bumped on every change (redraw, autosave).
    std::uint64_t Revision() const { return myRevision; }
    void          Touch() { ++myRevision; }
    /// Bumped when `look` changes: a new look needs no solve or rebuild.
    std::uint64_t LookRevision() const { return myLookRevision; }
    void          TouchLook() { ++myLookRevision; }

private:
    IdGenerator   myIds;
    std::uint64_t myRevision = 0;
    std::uint64_t myLookRevision = 0;
};

/// "Совмещение", "Вставка"... (Russian; the UI translates).
std::string_view ConstraintTitle(ConstraintType type);

} // namespace bz::assembly
