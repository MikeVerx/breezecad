#pragma once
// Touchpad gestures in the 3D view, as the settings assign them
// (app/TouchNavigation.h): two fingers pan, orbit or zoom (by modifier),
// pinch zooms, turning two fingers rolls or orbits, and the extra gestures
// run a command.

#include "app/AppContext.h"

namespace bz::app {

/// Radians of view turn per pixel the mouse or the fingers move (KOMPAS-like).
inline constexpr double kOrbitPerPixel = 0.0075;

/// Where the view turns about: the model point under `at`, else the model's
/// centre. Also notes the planar face there (the context menu offers a
/// sketch on it). Shared by the mouse and the touchpad.
void ChooseOrbitPivot(AppContext& app, const ViewportFrame& vf, ImVec2 at);

/// Applies this frame's gestures (app.touch) while the scene is under the
/// cursor. Returns true when there were finger scrolls or pinches this frame:
/// the system reports them as a mouse wheel too, which must then not zoom.
bool NavigateTouch(AppContext& app, const ViewportFrame& vf, bool sceneHovered);

/// Applies this frame's finger gestures on a touch screen (turn, move,
/// pinch; app.shell.mobile.gestures, sorted out by mobile/TouchInput).
/// Returns true while the view is being turned or moved.
bool NavigateFingers(AppContext& app, const ViewportFrame& vf);

} // namespace bz::app
