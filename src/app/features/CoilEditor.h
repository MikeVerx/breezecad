#pragma once
// The "Пружина" operation panel: a profile of a sketch wound around an осевая
// of the same sketch. The axis is clicked in the viewport; the panel holds
// the number of turns, the pitch and the hand.

#include "app/features/SketchOperationEditor.h"
#include "core/I18n.h"
#include "document/CoilFeature.h"

namespace bz::app {

class CoilEditor final : public SketchOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Пружина"); }
    std::string Hint() const override;

protected:
    const char* Icon() const override { return "tree-coil"; }
    std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const override;
    void LoadTexts(const doc::Feature& feature) override;
    void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) override;
    void CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const override;
    bool PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view) override;

private:
    std::string myPitch, myTurns;
};

} // namespace bz::app
