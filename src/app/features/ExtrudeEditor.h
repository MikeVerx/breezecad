#pragma once
// The "Выдавливание" operation panel. Lifecycle, profile picking and the
// panel frame come from SketchOperationEditor; this adds direction, depth,
// distance and taper.

#include "app/features/DragArrow.h"
#include "app/features/SketchOperationEditor.h"
#include "core/I18n.h"
#include "document/ExtrudeFeature.h"

namespace bz::app {

class ExtrudeEditor final : public SketchOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Выдавливание"); }
    std::string Hint() const override;
    /// Choosing the face "до": faces under the cursor light up.
    bool        PicksOriginal() const override { return myPickTarget; }

protected:
    const char* Icon() const override { return "tree-extrude"; }
    std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const override;
    void LoadTexts(const doc::Feature& feature) override;
    void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) override;
    void CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const override;
    /// The length arrow: drawn over the model, dragged to change the distance.
    bool PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view) override;

private:
    DragArrow   myArrow;
    DragArrow   myArrowBack;   ///< asymmetric: the distance on the other side
    std::string myDistance2;
    bool        myPickTarget = false;   ///< clicks in the view choose the plane "до", not profiles
    std::string myDistance;
    std::string myTaper;
};

} // namespace bz::app
