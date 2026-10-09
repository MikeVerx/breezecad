#include "document/FeatureRegistry.h"

#include "document/CoilFeature.h"
#include "document/DraftFeature.h"
#include "document/SectionFeature.h"
#include "document/EdgeFeature.h"
#include "document/ExtrudeFeature.h"
#include "document/HoleFeature.h"
#include "document/LoftFeature.h"
#include "document/PatternFeature.h"
#include "document/RevolveFeature.h"
#include "document/ShellFeature.h"
#include "document/SketchFeature.h"
#include "document/SweepFeature.h"
#include "document/WorkPlaneFeature.h"

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
        r.Register(std::string(SweepFeature::kType), [] { return std::make_unique<SweepFeature>(); });
        r.Register(std::string(CoilFeature::kType), [] { return std::make_unique<CoilFeature>(); });
        r.Register(std::string(DraftFeature::kType), [] { return std::make_unique<DraftFeature>(); });
        r.Register(std::string(SectionFeature::kType), [] { return std::make_unique<SectionFeature>(); });
        r.Register(std::string(LoftFeature::kType), [] { return std::make_unique<LoftFeature>(); });
        r.Register(std::string(PatternFeature::kType), [] { return std::make_unique<PatternFeature>(); });
        r.Register(std::string(MirrorFeature::kType), [] { return std::make_unique<MirrorFeature>(); });
        r.Register(std::string(ShellFeature::kType), [] { return std::make_unique<ShellFeature>(); });
        r.Register(std::string(WorkPlaneFeature::kType), [] { return std::make_unique<WorkPlaneFeature>(); });
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
