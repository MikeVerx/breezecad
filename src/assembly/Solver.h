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

#include <unordered_map>

namespace bz::assembly {

struct SolveOptions {
    /// A component that should move as little as possible (the one being
    /// dragged): the others give way first.
    ObjectId keep;
    int      maxIterations = 60;
    double   tolerance = 1e-7;   ///< mm (rad × 100 mm for angles)
};

struct SolveResult {
    bool   converged = false;   ///< every constraint holds
    bool   moved = false;       ///< some component changed its placement
    int    iterations = 0;
    double error = 0.0;         ///< what is left, mm
};

/// Moves free components; sets each constraint's `state` and `error`.
SolveResult Solve(Assembly& assembly, const SolveOptions& options = {});

/// Remaining freedom of every component (grounded ones: none), from the
/// constraints at the components' current places.
std::unordered_map<ObjectId, Freedom> AnalyzeFreedom(const Assembly& assembly);

/// The residual of one constraint at the current placements (0 = holds),
/// mm; for tests and the tree's tooltip.
double ConstraintError(const Assembly& assembly, const Constraint& constraint);

} // namespace bz::assembly
