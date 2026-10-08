#pragma once
// Lifecycle and panel frame shared by every operation panel.
//
// While the panel is open the feature already lives in the document, so the
// model shows its result (live preview). Применить/Готово turn it into one
// undo step; Отмена removes it (or restores the edited original).

#include "app/features/OperationEditor.h"

#include <memory>
#include <string>

namespace bz::app {

class FeatureOperationEditor : public OperationEditor {
public:
    bool IsOpen() const override { return myFeature.IsValid(); }
    void EditExisting(AppContext& app, Session& session, ObjectId feature) override;

    void Apply(AppContext& app, Session& session) override;
    void Done(AppContext& app, Session& session) override;
    void Cancel(AppContext& app, Session& session) override;

    /// Header (icon, title, close), the body from DrawBody, footer buttons.
    void DrawPanel(AppContext& app, Session& session) override;

protected:
    /// Icon of the panel header ("tree-extrude").
    virtual const char* Icon() const = 0;
    /// Fills the text fields from the feature (when the panel opens).
    virtual void LoadTexts(const doc::Feature& feature) = 0;
    /// The panel's own content.
    virtual void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) = 0;
    /// After Применить committed `previous`: open the next operation of the
    /// same kind. The default just closes the panel.
    virtual void StartNext(AppContext& app, Session& session, const doc::Feature& previous);

    /// Puts a new feature at the end of the part as a live preview (not yet
    /// an undo step) and opens the panel on it. Sets its id.
    void OpenNew(AppContext& app, Session& session, std::unique_ptr<doc::Feature> feature);
    /// The feature being edited (nullptr when it vanished).
    doc::Feature* Edited(Session& session) const;
    ObjectId      EditedId() const { return myFeature; }
    /// Marks the feature changed so the model rebuilds.
    void Changed(Session& session) const;

    void Section(AppContext& app, const char* label) const;
    void Gap(AppContext& app) const;
    void Caption(AppContext& app, const char* text, float width) const;
    /// Expression field for `parameter` with an "fx" variables menu. Enter
    /// finishes the operation. Returns true when the expression changed.
    bool ParameterField(AppContext& app, Session& session, const char* id, std::string& text, Parameter& parameter,
                        float width, bool focus, bool showFx = true);

    bool myFocusFirstField = false;   ///< focus the main value when the panel opens

private:
    bool Commit(AppContext& app, Session& session);
    void Reset();

    ObjectId                      myFeature;
    bool                          myIsNew = false;
    std::unique_ptr<doc::Feature> myOriginal;   ///< editing: the version to restore on Cancel
};

} // namespace bz::app
