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

    /// Left panel content.
    virtual void DrawPanel(AppContext& app, Session& session) = 0;
    /// Overlays and picking in the viewport.
    virtual void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) = 0;

    /// True when the viewport should let the user pick edges (fillet, chamfer).
    virtual bool PicksEdges() const { return false; }
    /// True when the viewport should pre-highlight faces under the cursor (hole).
    virtual bool PicksFaces() const { return false; }

    virtual const char* Title() const = 0;    ///< "Выдавливание", shown in the status bar
    virtual std::string Hint() const = 0;     ///< what to do, for the status bar
};

} // namespace bz::app
