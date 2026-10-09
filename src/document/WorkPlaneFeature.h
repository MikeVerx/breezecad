#pragma once
// A work plane: an origin plane, another work plane or a flat face of the
// body, moved along its normal by an offset. Sketches placed on it follow it
// when the offset (or the face) changes.

#include "document/FaceReference.h"
#include "document/Feature.h"

#include <optional>

namespace bz::doc {

class WorkPlaneFeature final : public FeatureBase<WorkPlaneFeature> {
public:
    static constexpr std::string_view kType = "workplane";

    /// The plane it starts from: an origin plane or a work plane before it.
    /// Unused when `face` is set.
    ObjectId      base;
    FaceReference face;          ///< a flat face of the body as the start
    bool          reversed = false;   ///< offset against the base's normal
    Parameter offset{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};

    /// Where the plane is after the last rebuild (runtime, never saved).
    std::optional<Frame> frame;

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void CollectParameters(std::vector<Parameter*>& out) override { out.push_back(&offset); }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// `base` moved along its normal by `distance`; sketch axes keep their directions.
Frame OffsetFrame(const Frame& base, double distance);

} // namespace bz::doc
