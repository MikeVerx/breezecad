#pragma once
// Shell ("Оболочка"): the most recent body made hollow with walls of a given
// thickness. The picked faces are removed (left open); with none picked the
// body becomes closed with a hollow inside. Walls go inward by default, so
// the outer size stays; `outward` keeps the inner size instead.

#include "document/FaceReference.h"
#include "document/Feature.h"

#include <vector>

namespace bz::doc {

class ShellFeature final : public FeatureBase<ShellFeature> {
public:
    static constexpr std::string_view kType = "shell";

    std::vector<FaceReference> faces;   ///< faces to remove (open)
    Parameter thickness{.name = "", .expression = "2", .quantity = Quantity::Length, .value = 2.0, .error = {}};
    bool outward = false;

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override { out.push_back(&thickness); }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
