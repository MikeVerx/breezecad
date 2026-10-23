#pragma once
// Edges of other parts projected into a part's sketch while it is edited in
// an assembly (doc::ContextProjection), and their following the source when
// it changes or moves: Inventor's adaptivity.
//
// Cheap by design: a part is looked at only when it has such projections;
// nothing is computed when the key of what they depend on (the sources'
// geometry, where they stand, the sketch planes) did not change; a sketch is
// solved and its part rebuilt only when a curve really moved.

#include "assembly/Assembly.h"
#include "document/Document.h"
#include "kernel/AssemblyModel.h"

#include <cstdint>
#include <string_view>

namespace bz::kernel {

/// `doc` has adaptive projections made in the assembly named `assembly`.
bool HasContextProjections(const doc::Document& doc, std::string_view assembly);

/// What those projections of the part of `target` depend on, as one number:
/// each source part's geometry, where it stands relative to `target`, and
/// the sketch planes. A change means the part may have to follow.
std::uint64_t ContextKey(const AssemblyModel& geometry, const assembly::Assembly& a, ObjectId target,
                         const doc::Document& doc, std::string_view assembly);

struct AdaptResult {
    int moved = 0;   ///< curves put where their source edges project now
    int lost = 0;    ///< source edges no longer found (their curves stay)
    bool changed() const { return moved > 0; }
};
/// Puts the adaptive projections of `doc` (the part of `target`) where their
/// sources project now. The sketches that changed are solved again and the
/// document marked changed from the first of them (its part then rebuilds).
AdaptResult AdaptContextProjections(const AssemblyModel& geometry, const assembly::Assembly& a, ObjectId target,
                                    doc::Document& doc, std::string_view assembly);

/// Projecting an edge of `source` into the part of `target` would make the
/// two follow each other round in a circle (the part of `source` already
/// depends on that of `target`, directly or through others): refused.
bool WouldCycle(const AssemblyModel& geometry, const assembly::Assembly& a, ObjectId source, ObjectId target,
                std::string_view assembly);

} // namespace bz::kernel
