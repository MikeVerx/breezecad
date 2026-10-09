#pragma once
// Holes drilled into a flat face of the existing body.
//
// The face is kept by its outward normal and a point on it, and found again
// on the rebuilt body (the planar face with that normal nearest the point).
// Centres live in the face's plane, in coordinates anchored to the model
// origin (see HolePlaneFrame), so they stay put when the face moves along
// its normal, e.g. when the part gets thicker.

#include "core/Math.h"
#include "document/Feature.h"

#include <vector>

namespace bz::doc {

struct FaceReference {
    Vec3 normal;   ///< outward, unit
    Vec3 point;    ///< a point of the face when it was picked
};

enum class HoleType : unsigned char { Simple, Counterbore, Countersink };

class HoleFeature final : public FeatureBase<HoleFeature> {
public:
    static constexpr std::string_view kType = "hole";

    FaceReference     face;
    std::vector<Vec2> centres;      ///< in HolePlaneFrame of the face
    HoleType          type = HoleType::Simple;
    bool              throughAll = false;
    Parameter diameter{.name = "", .expression = "6.6", .quantity = Quantity::Length, .value = 6.6, .error = {}};
    Parameter depth{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    /// Counterbore: diameter and depth of the wider part.
    Parameter headDiameter{.name = "", .expression = "11", .quantity = Quantity::Length, .value = 11.0, .error = {}};
    Parameter headDepth{.name = "", .expression = "6.4", .quantity = Quantity::Length, .value = 6.4, .error = {}};
    /// Countersink: diameter at the surface; the cone is 90°.
    Parameter sinkDiameter{.name = "", .expression = "13", .quantity = Quantity::Length, .value = 13.0, .error = {}};

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// The frame hole centres are measured in: on the plane through `point`
/// with `normal`, origin at the foot of the model origin, x along a stable
/// in-plane direction.
Frame HolePlaneFrame(const FaceReference& face);

} // namespace bz::doc
