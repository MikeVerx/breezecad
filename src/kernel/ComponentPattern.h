#pragma once
// Component patterns ("Массив" in an assembly): where the instances of a
// pattern go and how they are tied. The pattern itself (assembly::
// ComponentPattern) is data; this rebuilds its instances as components with
// constraints copied from the seed, so the solver holds them like any other.

#include "assembly/Assembly.h"
#include "core/Expected.h"
#include "kernel/AssemblyModel.h"

#include <functional>
#include <optional>
#include <vector>

namespace bz::kernel {

/// The group a pattern repeats when `component` is chosen: it and the
/// components tied to it through constraints that `joins` accepts (a bolt
/// brings its washer and nut). Grounded components never join. `component`
/// comes first, the rest in the assembly's order.
std::vector<ObjectId> PatternGroup(const assembly::Assembly& assembly, ObjectId component,
                                   const std::function<bool(const assembly::Component&)>& joins);

/// The rim of the hole a group sits in: the first "Вставка" that ties a
/// member to a circle of a component outside the group. Nullopt when none.
std::optional<assembly::GeometryRef> SeatHoleOf(const assembly::Assembly& assembly, const std::vector<ObjectId>& group);

/// One instance: how the seed moves to get there (in the assembly) and, for
/// a pattern of holes, the hole it goes into.
struct PatternSpot {
    assembly::Placement   move;
    assembly::GeometryRef hole;   ///< Holes only
};

/// Where the instances of `pattern` go (the seed's own place left out).
/// Errors say why there are none, in Russian.
Expected<std::vector<PatternSpot>> PatternSpots(const AssemblyModel& model, const assembly::Assembly& assembly,
                                                const assembly::ComponentPattern& pattern);

/// Rebuilds the instances of pattern `id`: removes the old ones and adds,
/// for each spot, copies of the seed components moved there with copies of
/// the seed's constraints. A constraint to a component outside the seed is
/// moved with the instance and kept when that geometry is found there
/// (the next hole of a plate); otherwise dropped, the copy then just stays
/// where it was put. Returns how many instances were made.
Expected<int> ApplyPattern(const AssemblyModel& model, assembly::Assembly& assembly, ObjectId id);

} // namespace bz::kernel
