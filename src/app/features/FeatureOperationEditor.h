#pragma once
// Lifecycle and panel frame shared by every operation panel.
//
// While the panel is open the feature already lives in the document, so the
// model shows its result (live preview). Применить/Готово turn it into one
// undo step; Отмена removes it (or restores the edited original).

#include "app/features/OperationEditor.h"
#include "document/FaceReference.h"
#include "document/ThreadFeature.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bz::app {

/// True when an active operation that makes a solid comes before `index`:
/// only then can a new one join, cut or intersect.
bool HasSolidBefore(const doc::Document& d, std::size_t index);

class FeatureOperationEditor : public OperationEditor {
public:
    bool     IsOpen() const override { return myFeature.IsValid(); }
    ObjectId EditedFeature() const override { return myFeature; }
    void EditExisting(AppContext& app, Session& session, ObjectId feature) override;

    void Apply(AppContext& app, Session& session) override;
    void Done(AppContext& app, Session& session) override;
    void Cancel(AppContext& app, Session& session) override;
    bool Ready(Session& session) const override;
    void Drop(AppContext&) override { Reset(); }
    bool StepBack(AppContext& app, Session& session) override;

    /// Header (icon, title, close), the body from DrawBody, footer buttons.
    void DrawPanel(AppContext& app, Session& session) override;

protected:
    /// Drops the feature's last pick; false when it has none (StepBack).
    virtual bool PopPick(doc::Feature& feature) { return false; }
    /// Icon of the panel header ("tree-extrude").
    const char* Icon() const override = 0;
    /// Fills the text fields from the feature (when the panel opens).
    virtual void LoadTexts(const doc::Feature& feature) = 0;
    /// The panel's own content.
    virtual void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) = 0;
    /// After Применить committed `previous`: open the next operation of the
    /// same kind. The default just closes the panel.
    virtual void StartNext(AppContext& app, Session& session, const doc::Feature& previous);
    /// False hides the feature's error under the panel, for errors that only
    /// say what to do next (the panel already says it).
    virtual bool ShowStatus(const doc::Feature& feature) const;

    /// Puts a new feature at the end of the part as a live preview (not yet
    /// an undo step) and opens the panel on it. Sets its id.
    void OpenNew(AppContext& app, Session& session, std::unique_ptr<doc::Feature> feature);
    /// The feature being edited (nullptr when it vanished).
    doc::Feature* Edited(Session& session) const;
    /// Marks the feature changed so the model rebuilds.
    void Changed(Session& session) const;

    void Section(AppContext& app, const char* label) const;
    void Gap(AppContext& app) const;
    void Caption(AppContext& app, const char* text, float width) const;
    /// Expression field for `parameter` with an "fx" variables menu. Enter
    /// finishes the operation. Returns true when the expression changed.
    bool ParameterField(AppContext& app, Session& session, const char* id, std::string& text, Parameter& parameter,
                        float width, bool focus, bool showFx = true);

    /// A list of the features before this one that `eligible` accepts, each
    /// with a check box; `chosen` keeps document order. Shows `emptyText`
    /// when there are none. Returns true when the choice changed.
    bool FeatureChecklist(AppContext& app, Session& session, std::vector<ObjectId>& chosen,
                          const std::function<bool(const doc::Feature&)>& eligible, float width, const char* emptyText);

    /// Flat faces of the body as it was before this feature: a click adds a
    /// face to `faces` or drops it. Call from DrawInViewport. True when changed.
    bool PickOriginalFaces(AppContext& app, Session& session, std::vector<doc::FaceReference>& faces, const ViewportFrame& vf);

    /// An origin or work plane before this feature: XY/XZ/YZ in the panel
    /// (PlaneChoice), any of them by a click in the viewport (PickPlane).
    void PlaneChoice(AppContext& app, Session& session, ObjectId& plane, float width);
    void PickPlane(AppContext& app, Session& session, ObjectId& plane, const ViewportFrame& vf);

    /// A chosen thing as a row ("Ребро: выбрано"), a warning when `value` is
    /// empty; the active one (where the next click goes) has a stronger
    /// border. True when clicked.
    static bool PickChip(ui::Ui& ui, const char* id, const std::string& caption, const std::string& value, bool active, float width);

    /// What a thread is («Резьба», a threaded hole): its kind, size, fit and
    /// lead-in chamfer. `size` is the nominal diameter; 0 means "by the face"
    /// (offered when `byFace`; `faceNominal` is what the face turned out to
    /// be, 0 when not known). `clearanceText` is the custom fit's field.
    /// Returns true when the kind or size changed: the standard pitch then
    /// belongs to the new size. Marks the feature changed itself.
    bool ThreadControls(AppContext& app, Session& session, doc::ThreadSpec& spec, double& size, double faceNominal,
                        std::string& clearanceText, float width, bool byFace);

    bool myFocusFirstField = false;   ///< focus the main value when the panel opens

private:
    bool Commit(AppContext& app, Session& session);
    void Reset();

    ObjectId                      myFeature;
    bool                          myIsNew = false;
    std::unique_ptr<doc::Feature> myOriginal;   ///< editing: the version to restore on Cancel
};

} // namespace bz::app
