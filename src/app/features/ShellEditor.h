#pragma once
// Оболочка: faces to remove are clicked in the viewport (again to drop one),
// the wall thickness is typed in the panel, walls go inward or outward.
// Like fillet, the viewport shows the body as it was before the operation for
// picking, so a face already removed can be clicked again.

#include "app/features/FeatureOperationEditor.h"
#include "core/I18n.h"

namespace bz::app {

class ShellEditor final : public FeatureOperationEditor {
public:
    void StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Оболочка"); }
    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;
    bool PicksOriginal() const override { return true; }
    std::string Hint() const override;

protected:
    const char* Icon() const override { return "tree-shell"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    std::string myThickness;
};

} // namespace bz::app
