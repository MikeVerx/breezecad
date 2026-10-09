#pragma once
// A solid stretched through the closed outlines of several sketches, in
// document order (typically sketches on work planes one above another).

#include "document/Feature.h"
#include "document/SketchSolidFeature.h"   // BooleanOp

#include <vector>

namespace bz::doc {

class LoftFeature final : public FeatureBase<LoftFeature> {
public:
    static constexpr std::string_view kType = "loft";

    std::vector<ObjectId> sections;   ///< sketches, each with one closed outline
    BooleanOp             operation = BooleanOp::NewBody;
    bool                  ruled = false;   ///< straight between sections instead of smooth

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override { return sections; }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
