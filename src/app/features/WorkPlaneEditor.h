#pragma once
// The "Плоскость" operation panel: the start is an origin plane, a work plane
// or a flat face clicked in the viewport; the offset is typed in the panel.

#include "app/features/DragArrow.h"
#include "app/features/FeatureOperationEditor.h"
#include "document/WorkPlaneFeature.h"

namespace bz::app {

class WorkPlaneEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Плоскость"; }
    std::string Hint() const override;
    bool        PicksFaces() const override { return true; }
    bool        DrawsPlanes() const override { return true; }

    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-plane"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    std::string myOffset;
    DragArrow   myArrow;   ///< offset
};

} // namespace bz::app
