#pragma once
// The "Оболочка" operation panel: flat faces clicked in the viewport become
// openings (again to close one), the wall thickness and side are set in the
// panel. Faces are picked on the body as it was; the preview shows the result.

#include "app/features/FeatureOperationEditor.h"
#include "document/ShellFeature.h"

namespace bz::app {

class ShellEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Оболочка"; }
    std::string Hint() const override;
    bool        PicksOriginal() const override { return true; }

    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-shell"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    std::string myThickness;
};

} // namespace bz::app
