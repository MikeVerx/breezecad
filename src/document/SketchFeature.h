#pragma once

#include "core/Math.h"
#include "document/EdgeReference.h"
#include "document/FaceReference.h"
#include "document/Feature.h"
#include "sketch/Sketch.h"

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

/// A model edge projected into the sketch ("Проецировать геометрию"): the
/// edge it shows and the sketch curve that shows it. The curve and its points
/// are marked `projected` (fixed for the solver); on every rebuild the kernel
/// finds the edge again and puts the curve where the edge projects now.
struct Projection {
    EdgeReference edge;
    ObjectId      curve;
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
