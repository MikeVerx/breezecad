#include "document/FeatureRegistry.h"

#include "document/EdgeFeature.h"
#include "document/ExtrudeFeature.h"
#include "document/HoleFeature.h"
#include "document/RevolveFeature.h"
#include "document/SketchFeature.h"

namespace bz::doc {

FeatureRegistry& FeatureRegistry::Default() {
    static FeatureRegistry registry = [] {
        FeatureRegistry r;
        r.Register(std::string(SketchFeature::kType), [] { return std::make_unique<SketchFeature>(); });
        r.Register(std::string(ExtrudeFeature::kType), [] { return std::make_unique<ExtrudeFeature>(); });
        r.Register(std::string(RevolveFeature::kType), [] { return std::make_unique<RevolveFeature>(); });
        r.Register(std::string(FilletFeature::kType), [] { return std::make_unique<FilletFeature>(); });
        r.Register(std::string(ChamferFeature::kType), [] { return std::make_unique<ChamferFeature>(); });
        r.Register(std::string(HoleFeature::kType), [] { return std::make_unique<HoleFeature>(); });
        return r;
    }();
    return registry;
}

void FeatureRegistry::Register(std::string typeId, Factory factory) {
    myFactories[std::move(typeId)] = std::move(factory);
}

std::unique_ptr<Feature> FeatureRegistry::Create(std::string_view typeId) const {
    const auto it = myFactories.find(typeId);
    return it != myFactories.end() ? it->second() : nullptr;
}

} // namespace bz::doc
