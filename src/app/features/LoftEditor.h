#pragma once
// The "Лофт" operation panel: tick the sketches to pass through (each with
// one closed outline), choose how the result combines with the body.

#include "app/features/FeatureOperationEditor.h"
#include "core/I18n.h"
#include "document/LoftFeature.h"

namespace bz::app {

class LoftEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Лофт"); }
    std::string Hint() const override;
    void        DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-loft"; }
    void LoadTexts(const doc::Feature& feature) override { (void)feature; }
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;
};

} // namespace bz::app
