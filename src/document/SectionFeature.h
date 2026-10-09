#pragma once
// "Сечение", as in KOMPAS: the model cut by a plane (an origin or work
// plane), one side kept. It sits in the tree like any operation, so it is
// switched off by suppressing it and removed by deleting it; the cut faces
// and edges are real geometry for later operations.

#include "document/FaceReference.h"
#include "document/Feature.h"

namespace bz::doc {

class SectionFeature final : public FeatureBase<SectionFeature> {
public:
    static constexpr std::string_view kType = "section";

    ObjectId      plane;         ///< origin or work plane (unused when `face` is set)
    FaceReference face;          ///< or a flat face of the body
    bool     flipped = false;    ///< keep the side the normal points to (by default it is cut away)

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
