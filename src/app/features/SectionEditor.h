#pragma once
// The "Сечение" operation panel: the plane and which side of it stays.

#include "app/features/FeatureOperationEditor.h"
#include "core/I18n.h"
#include "document/SectionFeature.h"

namespace bz::app {

class SectionEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Сечение"); }
    std::string Hint() const override;
    bool        DrawsPlanes() const override { return true; }
    bool        PicksOriginal() const override { return true; }
    bool        PicksFaces() const override { return true; }
    void        DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-cut-section"; }
    void LoadTexts(const doc::Feature&) override {}
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;
};

} // namespace bz::app
