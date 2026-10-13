#pragma once
// The "Точка" operation panel: a work point at a vertex, the centre of a
// round edge or sphere, the middle of an edge, where a line crosses a plane,
// or at typed coordinates (doc::WorkPointFeature).

#include "app/features/FeatureOperationEditor.h"
#include "document/WorkPointFeature.h"

namespace bz::app {

class WorkPointEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Точка"); }
    std::string Hint() const override;
    bool        DrawsPlanes() const override { return myMethod == doc::WorkPointMethod::Intersection; }
    bool        DrawsPoints() const override { return true; }
    bool        PicksOriginal() const override { return myMethod != doc::WorkPointMethod::Coordinates; }

    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-point"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;
    bool ShowStatus(const doc::Feature& feature) const override;

private:
    doc::WorkPointMethod myMethod = doc::WorkPointMethod::Vertex;
    int                  mySlot = 0;   ///< intersection: 0 the line, 1 the plane
    std::string          myX, myY, myZ;
};

} // namespace bz::app
