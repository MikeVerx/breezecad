#pragma once
// The "Плоскость" operation panel: offset from a start, half way between two
// starts, or through an edge at an angle (see doc::WorkPlaneFeature). Starts
// are origin planes, work planes or flat faces clicked in the viewport; the
// click goes to the highlighted field of the panel.

#include "app/features/DragArrow.h"
#include "app/features/FeatureOperationEditor.h"
#include "core/I18n.h"
#include "document/WorkPlaneFeature.h"

namespace bz::app {

class WorkPlaneEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Плоскость"); }
    std::string Hint() const override;
    bool        PicksFaces() const override { return true; }
    bool        DrawsPlanes() const override { return true; }
    /// Through an edge: the edges of the body before the plane are picked.
    bool        PicksOriginal() const override {
        return myMethod == doc::WorkPlaneMethod::Angle || myMethod == doc::WorkPlaneMethod::ThreePoints;
    }

    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-plane"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    std::string           myOffset, myAngle;
    doc::WorkPlaneMethod  myMethod = doc::WorkPlaneMethod::Offset;   ///< of the edited plane, last frame
    int                   mySlot = 0;   ///< where the next click goes: 0 the start, 1 the second start or the edge
    DragArrow   myArrow;   ///< offset
};

} // namespace bz::app
