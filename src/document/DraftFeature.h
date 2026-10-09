#pragma once
// "Уклон": side faces of the body tilted by an angle, for parts that must
// come out of a mould. The faces turn about the line where they cross the
// neutral plane (an origin or work plane); the pull direction is that
// plane's normal, or the opposite one.

#include "document/FaceReference.h"
#include "document/Feature.h"

#include <vector>

namespace bz::doc {

class DraftFeature final : public FeatureBase<DraftFeature> {
public:
    static constexpr std::string_view kType = "draft";

    std::vector<FaceReference> faces;
    ObjectId                   plane;              ///< neutral plane: origin or work plane
    bool                       reversed = false;   ///< pull against the plane's normal
    Parameter angle{.name = "", .expression = "3", .quantity = Quantity::Angle, .value = 3 * kPi / 180, .error = {}};

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void CollectParameters(std::vector<Parameter*>& out) override { out.push_back(&angle); }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
