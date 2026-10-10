#pragma once
// A work axis ("Ось"): a straight line in the model, for circular patterns
// and as a reference. Built one of three ways:
//   Edge      along a straight edge of the body;
//   Cylinder  the axis of a cylindrical face (a shaft, a hole);
//   Planes    where two planes (origin or work planes) meet;
//   Points    through two points: vertices of the body or work points.

#include "document/EdgeReference.h"
#include "document/Feature.h"
#include "document/WorkPlaneFeature.h"

#include <optional>
#include <utility>
#include <vector>

namespace bz::doc {

enum class WorkAxisMethod : unsigned char { Edge, Cylinder, Planes, Points };

class WorkAxisFeature final : public FeatureBase<WorkAxisFeature> {
public:
    static constexpr std::string_view kType = "workaxis";

    WorkAxisMethod               method = WorkAxisMethod::Cylinder;
    std::optional<EdgeReference> edge;
    std::optional<CylinderTouch> cylinder;   ///< `radial` unused
    ObjectId                     plane1, plane2;
    /// Points: the two points, as WorkPlaneFeature::points (see json_io::SavePointPicks).
    std::vector<Vec3>                         points;
    std::vector<std::optional<EdgeReference>> pointEdges;
    std::vector<ObjectId>                     pointFeatures;

    /// Where the axis is after the last rebuild: a point on it and its unit
    /// direction (runtime, never saved).
    std::optional<std::pair<Vec3, Vec3>> line;

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
