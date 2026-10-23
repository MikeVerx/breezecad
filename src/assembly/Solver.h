#pragma once
// Places components so their constraints hold, and tells how much freedom
// each component has left.
//
// Every free component has six unknowns: a shift and a small rotation.
// Constraints give equations in the components' poses (assembly coordinates).
// The solver takes the smallest change that satisfies them (damped least
// squares, Levenberg–Marquardt with the minimum-norm step): components that
// nothing pulls stay where they are, and a component dragged by the user
// moves only as far as its constraints let it.

#include "assembly/Assembly.h"

#include <optional>
#include <unordered_map>

namespace bz::assembly {

/// A point of a component pulled towards a place (the point grabbed while
/// dragging, towards the cursor): the component goes as near as its
/// constraints let it — a part on an axle turns, a slider slides.
struct Pull {
    ObjectId component;
    Vec3     local;    ///< the point, in the component's own coordinates
    Vec3     target;   ///< where it is pulled, in the assembly
};

struct SolveOptions {
    /// A component that should move as little as possible (the one being
    /// dragged): the others give way first.
    ObjectId keep;
    int      maxIterations = 60;
    double   tolerance = 1e-7;   ///< mm (rad × 100 mm for angles)
    /// Also tell which constraints do not hold or repeat others (states),
    /// as Analyze does. Off while a part is dragged: the states follow on
    /// release, the drag stays smooth.
    bool     analyze = true;
    /// Dragging: the grabbed point follows the cursor as far as the
    /// constraints allow (they still hold exactly afterwards).
    std::optional<Pull> pull;   // last: brace-initialised options keep their order
};

struct SolveResult {
    bool   converged = false;   ///< every constraint holds
    bool   moved = false;       ///< some component changed its placement
    int    iterations = 0;
    double error = 0.0;         ///< what is left, mm
};

/// Moves free components; sets each constraint's `state` and `error`
/// (unless `options.analyze` is off).
///
/// Components fall into independent groups (tied to each other by
/// constraints through free components); each group is solved by itself,
/// so a change in one corner of a large assembly costs only that corner.
SolveResult Solve(Assembly& assembly, const SolveOptions& options = {});

/// States of the constraints at the components' current places: which do
/// not hold (Failed) and which say again what others already say (Redundant).
void Analyze(Assembly& assembly);

/// The coordinate of a joint with one (Rotational: rad in (-π, π], Slider:
/// mm) at the components' current places; nullopt otherwise.
std::optional<double> JointCoordinate(const Assembly& assembly, ObjectId joint);

/// For a transmission just made (Gear, Screw): takes the joints' current
/// coordinates as the start (`driveAt`, `drivenAt`), so nothing moves.
void FitTransmission(const Assembly& assembly, Constraint& transmission);

/// Remaining freedom of every component (grounded ones: none), from the
/// constraints at the components' current places.
std::unordered_map<ObjectId, Freedom> AnalyzeFreedom(const Assembly& assembly);

/// The residual of one constraint at the current placements (0 = holds),
/// mm; for tests and the tree's tooltip.
double ConstraintError(const Assembly& assembly, const Constraint& constraint);

// --- joints ---------------------------------------------------------------------

/// The X of a joint frame in the part's coordinates: `g.reference` made
/// square to the direction, or a fixed one made from the direction.
Vec3 JointFrameX(const GeometryRef& g);

/// The coordinate of a one-coordinate joint (Rotational: rad in (-π, π],
/// Slider: mm) at the components' current places; nullopt for other types
/// or a missing component.
std::optional<double> JointPosition(const Assembly& assembly, const Constraint& joint);

/// For a joint just made where the parts stand: Z the way they face now
/// (`flipped`), the second frame's X turned to agree with the first's, so
/// that the coordinate starts at 0 (a slider: at the current shift) and
/// nothing turns when the joint is first solved.
void FitJoint(const Assembly& assembly, Constraint& joint);

/// Moves a one-coordinate joint to `position` (kept within its limits) and
/// places the rest by their constraints: the "drive" of a joint. A locked
/// joint keeps the new position; an unlocked one stays free. By default the
/// first component of the joint moves least.
SolveResult DriveJoint(Assembly& assembly, ObjectId joint, double position, SolveOptions options = {});

} // namespace bz::assembly
