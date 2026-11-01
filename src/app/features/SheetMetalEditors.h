#pragma once
// Sheet metal panels (docs/ADR/0007-sheet-metal.md).
//
// «Лист»: a closed sketch contour becomes a plate. Only the thickness has to
// be typed; the bend radius and the K-factor follow it («Авто») until the
// user switches one to «Вручную» and types a value.
// «Отбортовка»: an edge of the plate is clicked, the height and the angle are
// typed, the flange goes up or down.

#include "app/features/FeatureOperationEditor.h"
#include "app/features/SketchOperationEditor.h"
#include "core/I18n.h"
#include "document/SheetMetalFeature.h"

#include <optional>

namespace bz::app {

class SheetEditor final : public SketchOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Лист"); }
    std::string Hint() const override;

protected:
    const char* Icon() const override { return "tree-sheet"; }
    bool        ShowsOperation() const override { return false; }
    std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const override;
    void LoadTexts(const doc::Feature& feature) override;
    void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) override;

private:
    std::string myThickness, myRadius, myK;
};

class FlangeEditor final : public FeatureOperationEditor {
public:
    void StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Отбортовка"); }
    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;
    bool PicksOriginal() const override { return true; }
    std::string Hint() const override;

protected:
    bool PopPick(doc::Feature& feature) override {
        auto& f = static_cast<doc::FlangeFeature&>(feature);
        if (!f.edgeSet) return false;
        f.edgeSet = false;
        myMarked.reset();
        return true;
    }
    const char* Icon() const override { return "tree-flange"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;
    void StartNext(AppContext& app, Session& session, const doc::Feature& previous) override;
    bool ShowStatus(const doc::Feature& feature) const override;

private:
    std::string myHeight, myAngle, myOffset1, myOffset2, myWidth;
    std::optional<doc::EdgeReference> myMarked;   ///< the edge clicked, drawn over the model
};

/// «Вырез»: a closed contour of a sketch on a face of the sheet, through it.
class SheetCutEditor final : public SketchOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Вырез"); }
    std::string Hint() const override;
    bool        HasApply() const override { return false; }

protected:
    const char* Icon() const override { return "tree-sheet-cut"; }
    bool        ShowsOperation() const override { return false; }
    std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const override;
    void LoadTexts(const doc::Feature&) override {}
    void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) override;
};

/// The «Развёртка» window: the flat blank of the sheet metal part, its size
/// and mass, «Сохранить DXF».
void DrawFlatPatternWindow(AppContext& app);

/// The last «Лист» of the active part (none: no sheet metal).
ObjectId LastSheet(const doc::Document& d);

} // namespace bz::app
