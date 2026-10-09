#pragma once
// The "Вращение" operation panel: turns sketch regions around an axis. The
// axis is a sketch line picked in the viewport or one of the sketch axes.

#include "app/features/DragArrow.h"
#include "app/features/SketchOperationEditor.h"
#include "document/RevolveFeature.h"

namespace bz::app {

class RevolveEditor final : public SketchOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Вращение"; }
    std::string Hint() const override;

protected:
    const char* Icon() const override { return "tree-revolve"; }
    std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const override;
    void LoadTexts(const doc::Feature& feature) override;
    void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) override;
    void CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const override;
    bool PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view) override;

private:
    std::string myAngle;
    DragAngle   myAngleHandle;
};

} // namespace bz::app
