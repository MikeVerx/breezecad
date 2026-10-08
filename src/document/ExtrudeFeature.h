#pragma once

#include "core/Math.h"
#include "document/Feature.h"

namespace bz::doc {

/// How a new solid combines with the existing body.
enum class BooleanOp : unsigned char { NewBody, Join, Cut, Intersect };

/// How far an extrusion goes.
enum class ExtentType : unsigned char {
    Distance,     ///< `distance` along the direction
    Symmetric,    ///< `distance` in total, half to each side of the sketch
    ThroughAll,   ///< through the whole body (cuts)
};

/// Which closed regions of the sketch are extruded.
struct ProfileSelection {
    /// Automatic: every outer region, holes stay holes (a plate with a
    /// circle inside becomes a plate with a hole).
    bool automatic = true;
    /// Manual: one point inside each chosen region, in sketch coordinates.
    /// Points survive dimension changes better than region indices.
    std::vector<Vec2> regions;
};

class ExtrudeFeature final : public FeatureBase<ExtrudeFeature> {
public:
    static constexpr std::string_view kType = "extrude";

    ObjectId         sketch;
    ProfileSelection profiles;
    BooleanOp        operation = BooleanOp::NewBody;
    ExtentType       extent    = ExtentType::Distance;
    bool             reversed  = false;
    Parameter        distance{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    Parameter        taper{.name = "", .expression = "0", .quantity = Quantity::Angle, .value = 0.0, .error = {}};

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override { return {sketch}; }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

std::string_view BooleanOpId(BooleanOp op);
std::string_view ExtentTypeId(ExtentType extent);

} // namespace bz::doc
