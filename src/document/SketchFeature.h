#pragma once

#include "core/Math.h"
#include "document/EdgeReference.h"
#include "document/FaceReference.h"
#include "document/Feature.h"
#include "sketch/Sketch.h"

#include <optional>
#include <unordered_set>
#include <vector>

namespace bz::doc {

/// Where a sketch lies. Origin planes are exact; a sketch on a work plane
/// follows it; a sketch on a model face keeps the face's frame from the
/// moment it was created and is moved along with the face on rebuild when
/// the face can be found again (by `face`).
struct SketchPlacement {
    ObjectId      support;   ///< origin plane, work plane, or the feature that produced the face
    Frame         frame;     ///< plane in model space; sketch coordinates live in it
    bool          onFace = false;
    FaceReference face;      ///< onFace: the face as picked
};

/// The sketch curve a model edge was made from: an edge of an extrusion's
/// cap is the curve of its profile, moved along the sketch's normal. A
/// projection that knows it follows the curve rather than the edge, so a
/// fillet, chamfer, shell or any change that leaves the profile alone does
/// not break it: the line may get shorter, but it stays the line it was.
struct SketchSource {
    ObjectId sketch;            ///< the sketch feature (of the part the edge belongs to)
    ObjectId curve;             ///< its line, circle or arc
    double   offset = 0.0;      ///< where the edge was, along the sketch's normal, mm
    double   from = 0.0;        ///< the part of the curve the edge covered (0..1 along it;
    double   to = 1.0;          ///< a line from start to end, an arc counter-clockwise)

    bool IsSet() const { return sketch.IsValid() && curve.IsValid(); }
};

/// A model edge projected into the sketch ("Проецировать геометрию"): the
/// edge it shows and the sketch curve that shows it. The curve and its points
/// are marked `projected` (fixed for the solver); on every rebuild the kernel
/// finds the edge again and puts the curve where the edge projects now.
struct Projection {
    /// What is projected (Inventor's «Проецировать геометрию»): a model edge
    /// (a curve), a model vertex (a point), work geometry or the origin (a
    /// line where a plane crosses the sketch, an axis as a line or a point, a
    /// point), a curve or point of another sketch of the part, a piece of a
    /// sheet metal part's flat pattern.
    enum class Of : unsigned char { Edge, Vertex, Work, Sketch, Flat };

    EdgeReference edge;     ///< Edge; Vertex: an edge that ends at the vertex
    ObjectId      curve;    ///< the sketch curve, or point (Vertex, a point-like Work or Sketch)
    SketchSource  source;   ///< Edge: the profile curve the edge came from, when it has one;
                            ///< Sketch: the other sketch and its curve or point (offset 0, range 0..1)
    Of            of = Of::Edge;
    Vec3          at;       ///< Vertex: where the vertex was at the last rebuild
    ObjectId      work;     ///< Work: the work plane, axis, point or origin element; Flat: the «Лист»
    int           part = 0; ///< Flat: which curve — the outline's segments (outer loop, then holes), then the bend lines
};

/// An edge of another part of an assembly, projected into this part's
/// sketch while the part was edited in that assembly ("в контексте сборки").
/// The curve keeps where it was last put: the part opened on its own shows it
/// there. In the assembly it follows its source — another part changed or
/// moved — when `adaptive` (Inventor's adaptivity).
struct ContextProjection {
    std::string   assembly;          ///< the assembly it came from: its file name without extension
    ObjectId      component;         ///< the source component in that assembly
    EdgeReference edge;              ///< in the source part's own coordinates
    ObjectId      curve;
    bool          adaptive = true;   ///< follows the source; off: stays where it is
    SketchSource  source;            ///< the source part's profile curve the edge came from
};

class SketchFeature final : public FeatureBase<SketchFeature> {
public:
    static constexpr std::string_view kType = "sketch";

    SketchPlacement         placement;
    sketch::Sketch          sketch;
    std::vector<Projection> projections;
    std::vector<ContextProjection> contextProjections;

    /// Drops projections (of the part and from an assembly) whose curve was
    /// deleted from the sketch.
    void PruneProjections();

    /// Runtime analysis from the last solve, never saved.
    struct Analysis {
        int                          degreesOfFreedom = -1;   ///< -1: not solved yet
        std::unordered_set<ObjectId> fullyDefined;
        std::vector<ObjectId>        failing;                 ///< conflicting constraints
    } analysis;

    // --- Broken projections (Inventor's "sick" projected geometry) ------------
    // Runtime, never saved: found again on every rebuild and in the assembly.

    /// Curves of `projections` whose edge was not found (or projects as another
    /// kind of curve now) at the last rebuild: they stayed where they were.
    std::unordered_set<ObjectId> lostProjections;
    /// The same for `contextProjections`, at the last following in the assembly.
    std::unordered_set<ObjectId> lostContextProjections;
    /// The sketch as it was before its projections broke: what "Восстановить
    /// эскиз как до изменения" brings back. Kept while they stay broken.
    std::optional<sketch::Sketch> beforeBreak;

    /// Projected curves drawn red: lost ones, and those the sketch's constraints
    /// no longer agree with (a constraint on the curve or on its points fails
    /// after a change above it in the tree).
    std::unordered_set<ObjectId> BrokenProjections() const;
    bool HasBrokenProjections() const { return !BrokenProjections().empty(); }
    /// After projections were placed again and the sketch solved: `before` is
    /// the sketch from just before, `wasBroken` whether it was broken then.
    /// Keeps the sketch from before the break; forgets it once all is well.
    void NoteProjectionsUpdated(const sketch::Sketch& before, bool wasBroken);
    /// "Восстановить эскиз как до изменения": back to `beforeBreak` (as it is,
    /// when there is none), every projection unlinked from the model — its
    /// curves become ordinary geometry, held where they are by a Fix on each
    /// of their points. False when the sketch has no projections at all.
    bool RestoreBeforeBreak(IdGenerator& ids);

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void CollectParameters(std::vector<Parameter*>& out) override;
    void ReserveIds(IdGenerator& ids) const override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// File ids of constraint types ("coincident", "length"...).
std::string_view ConstraintTypeId(sketch::ConstraintType type);
bool ConstraintTypeFromId(std::string_view id, sketch::ConstraintType& out);

} // namespace bz::doc
