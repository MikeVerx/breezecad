#pragma once
// The "Выдавливание" operation panel: creates or edits an extrusion with a
// live preview. The feature lives in the document while the panel is open
// (so the model shows the result) and becomes one undo step on Apply/Done.

#include "app/AppContext.h"
#include "document/ExtrudeFeature.h"

#include <memory>
#include <string>

namespace bz::app {

class ExtrudeEditor {
public:
    bool IsOpen() const { return myFeature.IsValid(); }

    /// New extrusion of `sketch`, or of the selected / last unused sketch.
    void StartNew(AppContext& app, Session& session, ObjectId sketch = {});
    void EditExisting(AppContext& app, Session& session, ObjectId feature);

    void Apply(AppContext& app, Session& session);    ///< commit, stay open for the next one
    void Done(AppContext& app, Session& session);     ///< commit and close
    void Cancel(AppContext& app, Session& session);

    /// Left panel content.
    void DrawPanel(AppContext& app, Session& session);
    /// Sketch and profile regions in the viewport; clicks pick regions.
    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf);

    std::string Hint() const;
    void Reset();

private:
    doc::ExtrudeFeature* Feature(Session& session) const;
    void Changed(Session& session) const;
    bool Commit(AppContext& app, Session& session);

    ObjectId                      myFeature;
    bool                          myIsNew = false;
    std::unique_ptr<doc::Feature> myOriginal;   ///< editing: the version to restore on Cancel
    std::string                   myDistance;
    std::string                   myTaper;
    bool                          myFocusDistance = false;
    int                           myHoveredRegion = -1;
};

} // namespace bz::app
