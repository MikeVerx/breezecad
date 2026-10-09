#pragma once
// "Сдвиг": closed regions of one sketch carried along a path drawn in
// another sketch (a chain of lines and arcs, open or closed).

#include "document/SketchSolidFeature.h"

namespace bz::doc {

class SweepFeature final : public FeatureBase<SweepFeature, SketchSolidFeature> {
public:
    static constexpr std::string_view kType = "sweep";

    ObjectId path;   ///< the sketch with the path

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
