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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bz::kernel {

/// `doc` has adaptive projections made in the assembly named `assembly`.
bool HasContextProjections(const doc::Document& doc, std::string_view assembly);

/// The components those projections (and extrusions up to a face) take
/// their edges and faces from.
std::vector<ObjectId> ContextSources(const doc::Document& doc, std::string_view assembly);

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

// --- a change of the sketch another part is built from (ADR-0009) -----------------
// A fillet on the source part, or the source moved: the part follows quietly,
// as before. Its source sketch changed (a contour added, a line dragged or
// deleted): the author decides in the window «Обнаружено изменение,
// затрагивающее другие детали» what becomes of the part.

/// Per projected curve of `doc`'s adaptive projections from `assembly` that
/// know their source (doc::SketchSource): a fingerprint of that source curve
/// — its kind, shape and place in the source part (its sketch's plane
/// included, not where the source component stands). kSourceGone when the
/// curve or its sketch is gone; no print when the component (or its part) is
/// gone — that is not a change of the sketch, its projections just go red.
/// Cheap: no bodies are built, only the source sketches read.
using SourcePrints = std::unordered_map<ObjectId, std::uint64_t>;
inline constexpr std::uint64_t kSourceGone = 0;
SourcePrints SourcePrintsOf(const AssemblyModel& geometry, const doc::Document& doc, std::string_view assembly);

/// A sketch of the part whose source changed.
struct AffectedSketch {
    ObjectId              sketch;           ///< the sketch feature of the part
    std::string           name;             ///< as in the tree
    std::vector<ObjectId> curves;           ///< its projected curves whose source changed or is gone
    ObjectId              sourceComponent;  ///< where the (first) changed source is
    ObjectId              sourceSketch;     ///< and its sketch, in that component's part
};
/// The sketches of `doc` (in tree order) with a projected curve whose source
/// print differs between `before` (at the last following) and `now`. Curves
/// with no print in `before` (new) or in `now` (source component gone) are
/// not affected.
std::vector<AffectedSketch> FindAffectedSketches(const doc::Document& doc, std::string_view assembly, const SourcePrints& before,
                                                 const SourcePrints& now);

/// What the author chose for a part in the window.
enum class ChangeChoice : unsigned char {
    Ignore,   ///< «Игнорировать изменения»: the part stays as it is
    Adapt,    ///< «Автоматическое адаптирование»: the sketches follow the new contour
    Mimic,    ///< «Мимикрирование с зазором»: the new contour repeated at a gap
    Manual,   ///< «Ручное редактирование»: the sketches opened one by one
};

/// «Игнорировать изменения»: the affected curves' projections stop following
/// (adaptive off). Nothing of the part moves. How many were let go.
int IgnoreSourceChange(doc::Document& doc, std::string_view assembly, const std::vector<AffectedSketch>& affected);

/// «Мимикрирование с зазором». In each affected sketch the projections of
/// the changed source sketch are replaced by projections of the whole new
/// contour (the chain of the source sketch through the curves that are
/// left), and the outline drawn around the old ones (curves tied to them by
/// constraints, with their chains) by an offset of the new contour at `gap`
/// mm, on the side the old outline was (outside when there was none). The
/// old curves' other constraints go over to the nearest new curve of the
/// same kind. Only those chains are touched: nothing else is rebuilt. The
/// number of outline curves made; an error (and `doc` untouched) when the
/// source contour is gone or the offset cannot be built.
Expected<int> MimicSourceChange(const AssemblyModel& geometry, const assembly::Assembly& a, ObjectId target, doc::Document& doc,
                                std::string_view assembly, const std::vector<AffectedSketch>& affected, double gap);

} // namespace bz::kernel
