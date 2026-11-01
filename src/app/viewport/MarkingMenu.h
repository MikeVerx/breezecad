#pragma once
// The radial "hot" menu (as Inventor's marking menu, but seen at once): eight
// commands around the cursor.
//
// Called by a press and a move: the right button where it is free (the
// Inventor set), Shift + the left button where the right one turns the view
// (the BreezeCAD and KOMPAS sets) — or as chosen in Settings → Controls →
// Mouse. The menu shows the moment the cursor moves; release toward a
// command to run it, release in the middle to keep the menu and click one.
// Holding the right button still for a moment opens it too; a quick click
// stays the context menu (or a selection, with Shift + left).
//
// Eight commands per environment (sketch, part, assembly), clockwise from
// the top; the set can be changed in the settings.

#include "app/AppContext.h"

#include <array>
#include <string>

namespace bz::app {

enum class HotEnvironment : unsigned char { Sketch, Part, Assembly };

/// The built-in commands of an environment (action ids, clockwise from the top).
std::array<std::string, 8> HotMenuDefault(HotEnvironment env);
/// Those in use (the settings', or the built-in ones).
std::array<std::string, 8> HotMenuItems(const Settings& settings, HotEnvironment env);

enum class HotTrigger : unsigned char { Right, ShiftLeft, Off };
/// How the menu is called with these settings ("by the mouse set" resolved).
HotTrigger HotMenuTrigger(const Settings& settings);

/// Call right after the scene item. Returns true while the menu owns the
/// mouse: the overlays must not react to it (vf.hovered is cleared).
bool UpdateMarkingMenu(AppContext& app, Session& session, ViewportFrame& vf);

/// Draws the menu on top of the viewport (call after the overlays).
void DrawMarkingMenu(AppContext& app, Session& session);

} // namespace bz::app
