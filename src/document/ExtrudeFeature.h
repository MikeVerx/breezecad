#pragma once

#include "document/SketchSolidFeature.h"

namespace bz::doc {

/// How far an extrusion goes.
enum class ExtentType : unsigned char {
    Distance,     ///< `distance` along the direction
    Symmetric,    ///< `distance` in total, half to each side of the sketch
    ThroughAll,   ///< through the whole body (cuts)
};

/// Sweeps sketch regions along the sketch normal.
class ExtrudeFeature final : public FeatureBase<ExtrudeFeature, SketchSolidFeature> {
public:
    static constexpr std::string_view kType = "extrude";

    ExtentType extent   = ExtentType::Distance;
    bool       reversed = false;
    Parameter  distance{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    Parameter  taper{.name = "", .expression = "0", .quantity = Quantity::Angle, .value = 0.0, .error = {}};

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

std::string_view ExtentTypeId(ExtentType extent);

} // namespace bz::doc
