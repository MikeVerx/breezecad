#pragma once
// The "Ось" operation panel: a work axis along a straight edge, through the
// axis of a cylindrical face, where two planes meet or through two points
// (doc::WorkAxisFeature).

#include "app/features/FeatureOperationEditor.h"
#include "document/WorkAxisFeature.h"

namespace bz::app {

class WorkAxisEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Ось"); }
    std::string Hint() const override;
    bool        DrawsPlanes() const override { return myMethod == doc::WorkAxisMethod::Planes; }
    bool        DrawsPoints() const override { return myMethod == doc::WorkAxisMethod::Points; }
    /// An edge or a cylinder: picked on the body before the axis.
    bool        PicksOriginal() const override { return myMethod != doc::WorkAxisMethod::Planes; }

    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-axis"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    doc::WorkAxisMethod myMethod = doc::WorkAxisMethod::Cylinder;
    int                 mySlot = 0;   ///< two planes: which one the next click sets
    std::size_t         myPointSlot = 0;   ///< two points: the field the next click fills
};

} // namespace bz::app
