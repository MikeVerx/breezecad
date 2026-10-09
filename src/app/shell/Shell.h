#pragma once
// The window chrome around the viewport, following the approved mockup:
// header with the quick access toolbar, mode tabs, ribbon, left rail and
// panel, project tabs and the status bar. Plus the home screen and dialogs.

#include "app/AppContext.h"

namespace bz::app {

void  DrawHeader(AppContext& app, ImVec2 pos, float width);
void  DrawModeBar(AppContext& app, ImVec2 pos, float width);
/// Returns the ribbon height.
float DrawRibbon(AppContext& app, Session& session, ImVec2 pos, float width);
void  DrawLeftArea(AppContext& app, Session& session, ImVec2 pos, ImVec2 size);
void  DrawProjectTabs(AppContext& app, ImVec2 pos, float width);
void  DrawStatusBar(AppContext& app, ImVec2 pos, float width);
void  DrawHomeScreen(AppContext& app, ImVec2 pos, ImVec2 size);
void  DrawDialogs(AppContext& app);

// Panels of the left area (LeftPanels.cpp, TreePanel.cpp).
void DrawTreePanel(AppContext& app, Session& session, float width);
void DrawPropertiesPanel(AppContext& app, Session& session, float width);
void DrawVariablesPanel(AppContext& app, Session& session, float width);

/// Tree icon of a feature ("tree-extrude").
const char* FeatureIcon(const doc::Feature& f);

/// A fixed-size region of the root window that clips its contents.
/// Begins a child window at `pos`; returns its draw list.
ImDrawList* BeginArea(const char* id, ImVec2 pos, ImVec2 size, const Color& background, bool scroll = false);
void        EndArea();

} // namespace bz::app
