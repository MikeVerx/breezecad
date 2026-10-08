#include "document/Recompute.h"

#include "document/SketchFeature.h"
#include "sketch/Solver.h"

namespace bz::doc {

void RecomputeModel(Document& doc) {
    doc.EvaluateParameters();

    for (std::size_t i = 0; i < doc.FeatureCount(); ++i) {
        Feature& f = *doc.Features()[i];
        if (!doc.IsActive(i)) {
            f.status = {FeatureState::NotComputed, {}};
            continue;
        }
        if (f.suppressed) {
            f.status = {FeatureState::Suppressed, {}};
            continue;
        }
        auto* sk = dynamic_cast<SketchFeature*>(&f);
        if (!sk) continue;

        std::string dimensionError;
        for (const Parameter* p : sk->Parameters())
            if (p->HasError() && dimensionError.empty()) dimensionError = p->name + ": " + p->error;

        const sketch::SolveResult solved = sketch::Solve(sk->sketch);
        sk->analysis.degreesOfFreedom = solved.converged ? solved.degreesOfFreedom : -1;
        sk->analysis.fullyDefined     = solved.fullyDefined;
        sk->analysis.failing          = solved.failing;

        if (!solved.converged)
            sk->status = {FeatureState::Error, "Ограничения эскиза противоречат друг другу"};
        else if (!dimensionError.empty())
            sk->status = {FeatureState::Warning, dimensionError};
        else
            sk->status = {FeatureState::Ok, {}};
    }
}

} // namespace bz::doc
