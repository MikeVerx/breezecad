#pragma once
// An operation panel in the left area (Выдавливание, Вращение...): it edits
// one feature with a live preview and ends with Готово, Применить or Отмена.
// Only one operation is open at a time; the rest of the app talks to
// whichever is open through this interface.

#include "app/AppContext.h"

#include <string>

namespace bz::doc {
class Feature;
}

namespace bz::app {

class OperationEditor {
public:
    virtual ~OperationEditor() = default;

    virtual bool IsOpen() const = 0;
    /// The feature being created or edited (its errors show in the panel).
    virtual ObjectId EditedFeature() const = 0;
    /// True when this editor edits features like `f` (double click, "Редактировать").
    virtual bool CanEdit(const doc::Feature& f) const = 0;
    virtual void EditExisting(AppContext& app, Session& session, ObjectId feature) = 0;

    virtual void Apply(AppContext& app, Session& session) = 0;   ///< commit, stay open for the next one
    virtual void Done(AppContext& app, Session& session) = 0;    ///< commit and close
    virtual void Cancel(AppContext& app, Session& session) = 0;
    /// The document it worked on is gone (closed): forget it without touching any document.
    virtual void Drop(AppContext& app) { (void)app; }
    /// The document it was opened in (nullptr: not known).
    const Session* Owner() const { return myOwner; }

protected:
    const Session* myOwner = nullptr;   ///< set when it opens (Application drops it when another document comes forward)

public:

    /// Left panel content.
    virtual void DrawPanel(AppContext& app, Session& session) = 0;
    /// Overlays and picking in the viewport.
    virtual void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) = 0;

    /// True when the operation picks on the body as it was before it (fillet,
    /// shell): the editor calls Viewport::BeginPickingOriginal each frame.
    virtual bool PicksOriginal() const { return false; }
    /// True when the viewport should pre-highlight faces under the cursor (hole).
    virtual bool PicksFaces() const { return false; }
    /// True when the operation draws the origin and work planes itself (work plane).
    virtual bool DrawsPlanes() const { return false; }
    /// True when the operation draws the work points itself (it picks them).
    virtual bool DrawsPoints() const { return false; }

    /// True when "Применить" (commit and stay open) differs from "Готово"; the
    /// phone shows it in block 1's bottom line then.
    virtual bool HasApply() const { return true; }

    /// Everything needed is chosen: "Применить" would make something (the phone's ✓ turns green).
    virtual bool Ready(Session& session) const { return true; }
    /// Takes back the last pick (a contour, an edge, a face); false when nothing is left to take
    /// back — the phone's ✕ then closes the operation.
    virtual bool StepBack(AppContext& app, Session& session) { return false; }

    virtual const char* Title() const = 0;    ///< "Выдавливание", shown in the status bar
    virtual const char* Icon() const { return "ui-wrench"; }   ///< its icon, next to the title
    virtual std::string Hint() const = 0;     ///< what to do, for the status bar
};

} // namespace bz::app
