#pragma once
// The "Уклон" operation panel: side faces clicked in the viewport (again to
// drop one), the neutral plane, the pull direction and the angle.

#include "app/features/FeatureOperationEditor.h"
#include "document/DraftFeature.h"

namespace bz::app {

class DraftEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Уклон"; }
    std::string Hint() const override;
    bool        PicksOriginal() const override { return true; }
    bool        DrawsPlanes() const override { return true; }
    void        DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-draft"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    std::string myAngle;
};

} // namespace bz::app
