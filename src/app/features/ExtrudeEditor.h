#pragma once
// The "Выдавливание" operation panel. Lifecycle, profile picking and the
// panel frame come from SketchOperationEditor; this adds direction, depth,
// distance and taper.

#include "app/features/SketchOperationEditor.h"
#include "document/ExtrudeFeature.h"

namespace bz::app {

class ExtrudeEditor final : public SketchOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Выдавливание"; }
    std::string Hint() const override;

protected:
    const char* Icon() const override { return "tree-extrude"; }
    std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const override;
    void LoadTexts(const doc::SketchSolidFeature& feature) override;
    void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) override;
    void CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const override;

private:
    std::string myDistance;
    std::string myTaper;
};

} // namespace bz::app
