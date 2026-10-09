#pragma once
// "Пружина", as in Inventor: a closed profile of a sketch wound along a helix
// around an осевая line of the same sketch. Joined to or cut from a body it
// also makes a ridge or a groove (a simple thread).

#include "document/SketchSolidFeature.h"

namespace bz::doc {

class CoilFeature final : public FeatureBase<CoilFeature, SketchSolidFeature> {
public:
    static constexpr std::string_view kType = "coil";

    ObjectId axisLine;            ///< a line of the sketch, normally an осевая
    bool     leftHanded = false;
    Parameter pitch{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    Parameter turns{.name = "", .expression = "5", .quantity = Quantity::Scalar, .value = 5.0, .error = {}};

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
