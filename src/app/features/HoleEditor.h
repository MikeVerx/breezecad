#pragma once
// The "Отверстие" operation panel. The first click on a flat face of the body
// chooses the face and puts the first centre there; further clicks on the
// same face add centres, a click on a centre removes it. Centres can also be
// typed exactly in the panel.

#include "app/features/DragArrow.h"
#include "app/features/FeatureOperationEditor.h"
#include "core/I18n.h"
#include "document/HoleFeature.h"

#include <optional>

namespace bz::app {

class HoleEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Отверстие"); }
    std::string Hint() const override;
    bool        PicksFaces() const override { return true; }
    /// "Соосно": round edges and cylinders of the body as it was are clickable.
    bool        PicksOriginal() const override { return myCoaxial; }

    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-hole"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;
    void StartNext(AppContext& app, Session& session, const doc::Feature& previous) override;
    bool ShowStatus(const doc::Feature& feature) const override;

private:
    doc::HoleFeature* Feature(Session& session) const;
    void DrawFaceSection(AppContext& app, Session& session, doc::HoleFeature& f, float width);
    void DrawCentres(AppContext& app, Session& session, doc::HoleFeature& f, float width);
    void DrawSizes(AppContext& app, Session& session, doc::HoleFeature& f, float width);
    /// Резьбовое отверстие: the size from the list and the pitch.
    void DrawThread(AppContext& app, Session& session, doc::HoleFeature& f, float width);
    /// A click on the model: choose the face or add a centre on it.
    void ClickModel(AppContext& app, Session& session, doc::HoleFeature& f, const ViewportFrame& vf);
    /// "Соосно": a click on a round edge or a cylindrical face adds a centre
    /// held on its axis (a flat face first chooses the face, as usual).
    void ClickCoaxial(AppContext& app, Session& session, doc::HoleFeature& f, const ViewportFrame& vf);

    std::string        myDiameter, myDepth, myHeadDiameter, myHeadDepth, mySinkDiameter;
    std::string        myThreadPitch, myThreadDepth;
    std::optional<int> myHoveredCentre;
    DragArrow          myArrow;   ///< depth
    bool               myCoaxial = false;   ///< clicks make coaxial centres
};

} // namespace bz::app
