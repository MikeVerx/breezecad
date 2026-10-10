#pragma once
// The radial menu (as Inventor's marking menu): hold the right button still
// over the model for a moment and eight commands appear around the cursor
// (moving it instead orbits, a short click opens the context menu).
// Move toward one and release to run it; release in the middle to keep the
// menu open and click a command. What is offered depends on the mode: model,
// sketch, or an operation being edited.

#include "app/AppContext.h"

namespace bz::app {

/// Call right after the scene item. Returns true while the menu owns the
/// mouse: the overlays must not react to it (vf.hovered is cleared).
bool UpdateMarkingMenu(AppContext& app, Session& session, ViewportFrame& vf);

/// Draws the menu on top of the viewport (call after the overlays).
void DrawMarkingMenu(AppContext& app, Session& session);

} // namespace bz::app
