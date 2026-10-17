#pragma once
// The window chrome around the viewport, following the approved mockup:
// header with the quick access toolbar, mode tabs, ribbon, left rail and
// panel, project tabs and the status bar. Plus the home screen and dialogs.

#include "app/AppContext.h"

namespace bz::app {

void  DrawHeader(AppContext& app, ImVec2 pos, float width);
/// The main menu (the logo's): call right after the button that opens
/// "##main-menu", in the same window.
void  MainMenuPopup(AppContext& app);
void  DrawModeBar(AppContext& app, ImVec2 pos, float width);
/// Returns the ribbon height.
float DrawRibbon(AppContext& app, Session& session, ImVec2 pos, float width);
void  DrawLeftArea(AppContext& app, Session& session, ImVec2 pos, ImVec2 size);

/// Where the commands of a ribbon tab go: the desktop's ribbon lays them
/// out in groups (Ribbon.cpp), the phone's block 2 lists them
/// (mobile/MobileRibbon.cpp). Same content, one place to change it.
class RibbonSink {
public:
    virtual ~RibbonSink() = default;
    virtual AppContext& App() const = 0;
    virtual void BeginGroup(const char* caption) = 0;
    virtual void EndGroup() = 0;
    virtual void Large(const char* actionId, const char* caption, bool active = false) = 0;
    virtual void LargeLater(const char* icon, const char* caption) = 0;
    virtual void BeginColumn() = 0;
    virtual void Small(const char* actionId, const char* label = nullptr, bool active = false) = 0;
    virtual void SmallLater(const char* icon, const char* label) = 0;
    virtual void EndColumn() = 0;
    virtual void BeginGrid(int columns) = 0;
    virtual void Icon(const char* actionId, bool active = false) = 0;
    virtual void EndGrid() = 0;
};
/// The commands of the current ribbon tab (app.shell.ribbonTab) into `sink`.
/// `withFinish`: "Принять эскиз" among them (the phone pins it above the list instead).
void FillRibbon(AppContext& app, Session& session, RibbonSink& sink, bool withFinish);
void  DrawProjectTabs(AppContext& app, ImVec2 pos, float width);
void  DrawStatusBar(AppContext& app, ImVec2 pos, float width);
void  DrawHomeScreen(AppContext& app, ImVec2 pos, ImVec2 size);
void  DrawDialogs(AppContext& app);
/// "В библиотеку" (SaveToLibrary.cpp): opens the window for the active part.
void  OpenSaveToLibrary(AppContext& app);
void  DrawSaveToLibrary(AppContext& app);
/// A centred modal window of the house style; false when it is not shown.
bool  BeginDialog(ui::Ui& ui, const char* id, bool& open, float width);
/// The "Создать" window (NewDocumentDialog.cpp); opened by OpenNewDocumentDialog.
void  DrawNewDocumentDialog(AppContext& app);

// Panels of the left area (LeftPanels.cpp, TreePanel.cpp).
void DrawTreePanel(AppContext& app, Session& session, float width);
void DrawAssemblyTree(AppContext& app, Session& session, float width);
void DrawPropertiesPanel(AppContext& app, Session& session, float width);
void DrawVariablesPanel(AppContext& app, Session& session, float width);

/// Tree icon of a feature ("tree-extrude").
const char* FeatureIcon(const doc::Feature& f);

/// A fixed-size region of the root window that clips its contents.
/// Begins a child window at `pos`; returns its draw list.
ImDrawList* BeginArea(const char* id, ImVec2 pos, ImVec2 size, const Color& background, bool scroll = false);
void        EndArea();

} // namespace bz::app
