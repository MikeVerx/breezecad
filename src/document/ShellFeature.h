#pragma once
// Hollows the body out, leaving walls of a given thickness. The flat faces
// listed in `openFaces` are removed and become openings (a box becomes a
// tray); with none the body becomes a closed hollow.

#include "document/FaceReference.h"
#include "document/Feature.h"

#include <vector>

namespace bz::doc {

class ShellFeature final : public FeatureBase<ShellFeature> {
public:
    static constexpr std::string_view kType = "shell";

    std::vector<FaceReference> openFaces;
    /// Walls grow inward from the outer surface (the part keeps its size);
    /// outward keeps the inside instead.
    bool      outward = false;
    Parameter thickness{.name = "", .expression = "2", .quantity = Quantity::Length, .value = 2.0, .error = {}};

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override { out.push_back(&thickness); }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
