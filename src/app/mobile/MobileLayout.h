#pragma once
// The phone layout (docs/ADR/0004-mobile-touch-ui.md, "Мобильная раскладка"):
// the program's own panels and tools, arranged for two thumbs on a phone held
// sideways.
//
//   ┌──────┬──────────────────────────────┬────────┐
//   │ brow │  [5 undo/redo] [3 tabs]   ▣  │  brow  │
//   │  1   │                              │   2    │
//   │ tree │          the model           │ tools  │
//   │ tool │                              │ (list) │
//   │ dims │  xyz                         │        │
//   │ row  │  [4 home | projects…]        │        │
//   │beard │                              │ beard  │
//   └──────┴──────────────────────────────┴────────┘
//
// Block 1 (tree / tool / dimensions and the row: frame · cursor · cancel ·
// OK, OK nearest the model), block 2 (the commands of the tab as a list),
// block 3 (the tabs) and 5 (undo/redo) float over the model, block 4 is the
// strip of open projects. Blocks 1 and 2 change sides for the left hand.
// Where the camera cuts into the screen, a dark panel along that edge holds
// the logo, the user's hotbar and the colours.
//
// A tablet (the window's shorter side 600 dp or more) keeps the desktop
// layout, a little bigger.

#include "app/AppContext.h"

namespace bz::platform {
class Window;
}

namespace bz::app::mobile {

/// The phone layout for this window now: by its size in dp (not its
/// proportions, which lie on folding phones and in split screen), unless
/// the settings force one. Never on the desktop.
bool WantsPhoneLayout(const Settings& settings, const platform::Window& window);

/// The whole window in the phone layout (instead of Application::DrawRoot).
void DrawPhoneRoot(AppContext& app);

/// The error screen after a crash or a hang last time (Android, layer 3).
void DrawLastCrash(AppContext& app);

/// The phone's first save (and «Сохранить как»): a window with the file's
/// name and its folder — the projects folder; a tap on it opens the system's
/// folder picker and the folder chosen becomes the projects folder (the
/// author, 22.10.2026). `closeAfter`: the tab closes once it is saved.
void AskSaveName(AppContext& app, Session& session, bool closeAfter);
void DrawSaveAsk(AppContext& app);

/// Block 2's commands (MobileRibbon.cpp): compact (four buttons a group) or the long list.
void DrawToolList(AppContext& app, Session& session, float width, bool iconsOnly, bool compact, bool viewOnLeft);
/// Compact block 2: the open group's strip over the view.
void DrawToolStrip(AppContext& app, ImVec2 viewMin, ImVec2 viewMax, bool blockOnRight);

/// The layout editor (full screen, mockups 2a–2d) and the first-start setup
/// (5a–5b): LayoutEditor.cpp. Drawn last, over everything.
void DrawLayoutEditor(AppContext& app);
void DrawSetup(AppContext& app);
/// Opens the layout editor (from the settings) or the first-start setup.
void OpenLayoutEditor(AppContext& app);
void StartSetup(AppContext& app);

/// The numpad (Numpad.cpp): its height, drawing it, and a key typed.
float NumpadHeight(const ui::Ui& ui);
void  DrawNumpad(AppContext& app, ImVec2 pos, ImVec2 size);
void  FireNumpadKey(AppContext& app, int code);

/// Files on the phone, in the main menu: the projects folder and "share".
void PhoneFileMenu(AppContext& app);

} // namespace bz::app::mobile
