#pragma once
// "Пружина": a round wire wound along a helix around a model axis through
// the origin. Joined to or cut from the body it also makes a ridge or a
// groove (a simple thread).

#include "document/Feature.h"
#include "document/PatternFeature.h"       // ModelAxis
#include "document/SketchSolidFeature.h"   // BooleanOp

namespace bz::doc {

class CoilFeature final : public FeatureBase<CoilFeature> {
public:
    static constexpr std::string_view kType = "coil";

    ModelAxis axis = ModelAxis::Z;
    BooleanOp operation = BooleanOp::NewBody;
    bool      leftHanded = false;
    Parameter radius{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    Parameter pitch{.name = "", .expression = "5", .quantity = Quantity::Length, .value = 5.0, .error = {}};
    Parameter turns{.name = "", .expression = "6", .quantity = Quantity::Scalar, .value = 6.0, .error = {}};
    Parameter wire{.name = "", .expression = "2", .quantity = Quantity::Length, .value = 2.0, .error = {}};   ///< wire diameter
    Parameter start{.name = "", .expression = "0", .quantity = Quantity::Length, .value = 0.0, .error = {}};  ///< along the axis

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
