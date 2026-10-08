#pragma once
// Shared panel logic of operations that turn sketch regions into a solid
// (extrude, revolve). The feature lives in the document while the panel is
// open, so the model shows the result, and becomes one undo step on
// Применить/Готово. A derived editor only describes its own options.

#include "app/features/OperationEditor.h"
#include "app/sketch/SketchView.h"
#include "document/SketchSolidFeature.h"

#include <memory>
#include <string>

namespace bz::app {

class SketchOperationEditor : public OperationEditor {
public:
    bool IsOpen() const override { return myFeature.IsValid(); }

    /// New operation on `sketch`, or on the selected / last unused sketch.
    void StartNew(AppContext& app, Session& session, ObjectId sketch = {});
    void EditExisting(AppContext& app, Session& session, ObjectId feature) override;

    void Apply(AppContext& app, Session& session) override;
    void Done(AppContext& app, Session& session) override;
    void Cancel(AppContext& app, Session& session) override;

    void DrawPanel(AppContext& app, Session& session) override;
    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    /// Icon of the panel header ("tree-extrude").
    virtual const char* Icon() const = 0;
    /// A new feature of this kind for `sketch`, with defaults; id and name are set by the caller.
    virtual std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const = 0;
    /// Fills the text fields from the feature (when the panel opens).
    virtual void LoadTexts(const doc::SketchSolidFeature& feature) = 0;
    /// The operation's own options, below "Операция" and "Профиль".
    virtual void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) = 0;
    /// After Применить: settings worth keeping for the next operation.
    virtual void CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const;
    /// Viewport picking before regions (revolve picks its axis). Returns
    /// true when it consumed the click.
    virtual bool PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view);

    // --- helpers for derived editors ---------------------------------------
    doc::SketchSolidFeature* Feature(Session& session) const;
    /// Marks the feature changed so the model rebuilds.
    void Changed(Session& session) const;
    void Section(AppContext& app, const char* label) const;
    void Gap(AppContext& app) const;
    /// Expression field for `parameter` with an "fx" variables menu. Enter
    /// finishes the operation. Returns true when the expression changed.
    bool ParameterField(AppContext& app, Session& session, const char* id, std::string& text, Parameter& parameter,
                        float width, bool focus, bool showFx = true);

    bool myFocusFirstField = false;   ///< focus the main value when the panel opens

private:
    bool Commit(AppContext& app, Session& session);
    void Reset();
    void DrawProfileSection(AppContext& app, Session& session, doc::SketchSolidFeature& f, float width);

    ObjectId                      myFeature;
    bool                          myIsNew = false;
    std::unique_ptr<doc::Feature> myOriginal;   ///< editing: the version to restore on Cancel
    int                           myHoveredRegion = -1;
};

} // namespace bz::app
