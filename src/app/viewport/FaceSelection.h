#pragma once
// Choosing faces of the part in the view, as in Inventor and KOMPAS: a
// click chooses a face (an empty spot clears the choice), Shift or Ctrl with
// a click adds or removes one, a drag with the left button draws a frame
// (left to right: the faces wholly inside; right to left: every face it
// touches; with Shift they are added, with Ctrl removed). A right click on
// a face not chosen chooses it, and the context menu then offers what can be
// done with the chosen faces (an appearance or a picture on them, ...).

#include "app/AppContext.h"

namespace bz::app {

/// Once a frame in the viewport, after the navigation (Model mode of a part,
/// no operation open, not measuring; otherwise it only hides the choice).
void UpdateFaceSelection(AppContext& app, Session& session, const ViewportFrame& vf);

/// The context-menu items for the chosen faces (nothing when none are chosen).
void FaceSelectionMenu(AppContext& app, Session& session);

} // namespace bz::app
