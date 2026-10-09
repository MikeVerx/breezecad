#pragma once

#include "sketch/Sketch.h"

#include <optional>
#include <unordered_set>
#include <vector>

namespace bz::sketch {

/// A point the user is dragging: the solver tries to put it at `target`
/// and moves the rest of the sketch as little as possible.
struct Drag {
    ObjectId point;
    Vec2     target;
};

struct SolveOptions {
    /// Points being dragged. Several points move together (dragging a
    /// whole line); if that is impossible only the first one is tried.
    std::vector<Drag> drags;
    int                 maxIterations = 60;
};

struct SolveResult {
    /// All constraints are satisfied. When false the sketch is left as it
    /// was before the call.
    bool converged = false;

    /// Remaining degrees of freedom. 0 means the sketch is fully defined.
    int degreesOfFreedom = 0;

    /// Points and curves that can no longer move (drawn in a "defined" colour).
    std::unordered_set<ObjectId> fullyDefined;

    /// Constraints that could not be satisfied when `converged` is false.
    std::vector<ObjectId> failing;
};

/// Moves sketch geometry so that every constraint holds.
///
/// Uses damped Gauss-Newton with minimum-norm steps: an under-defined
/// sketch moves as little as possible, which keeps it stable while the
/// user edits it. Dimension values are read from each constraint's
/// `parameter.value`, so evaluate parameters before solving.
SolveResult Solve(Sketch& sketch, const SolveOptions& options = {});

/// Analyses the sketch without moving anything: degrees of freedom and
/// fully defined entities.
SolveResult Analyze(const Sketch& sketch);

/// True when `candidate` would add no new restriction to the sketch:
/// it is already implied by other constraints (or contradicts them).
/// CAD convention: such a constraint is not added; a dimension may be
/// added as a reference (driven) one instead.
bool IsRedundant(const Sketch& sketch, const Constraint& candidate);

} // namespace bz::sketch
