#pragma once
// The "Сдвиг" operation panel: regions of the profile sketch are picked in
// the viewport like for an extrusion; the path is another sketch, chosen in
// the panel.

#include "app/features/SketchOperationEditor.h"
#include "document/SweepFeature.h"

namespace bz::app {

class SweepEditor final : public SketchOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Сдвиг"; }
    std::string Hint() const override;
    void        DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-sweep"; }
    ObjectId ChooseSketch(const Session& session, ObjectId proposed) const override;
    std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const override;
    void LoadTexts(const doc::Feature& feature) override { (void)feature; }
    void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) override;
};

} // namespace bz::app
