#pragma once
// State of the phone layout and of touch input that outlives a frame
// (docs/ADR/0004-mobile-touch-ui.md). Kept in ShellState::mobile; empty and
// unused on the desktop.

#include "app/mobile/Gestures.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace bz::app::mobile {

/// What the pointer is (Android's tool types).
enum class PointerTool : unsigned char { Finger, Stylus, Eraser, Mouse };
enum class PointerAction : unsigned char { Down, Move, Up, Cancel, HoverMove, HoverExit, Scroll };

/// The numpad's keys that are not characters.
enum NumpadCode : int { NumpadBackspace = -1, NumpadPrevField = -2, NumpadNextField = -3, NumpadKeyboard = -4, NumpadEnter = -5,
                        NumpadPrevious = -6 };

/// One pointer event from the window, in window coordinates (dp).
struct PointerEvent {
    int           id = 0;
    PointerTool   tool = PointerTool::Finger;
    PointerAction action = PointerAction::Move;
    float         x = 0, y = 0;
    int           buttons = 0;     ///< Mouse: 1 left, 2 right, 4 middle. Stylus: 2 its first button, 4 its second
    float         scrollX = 0, scrollY = 0;   ///< Scroll (mouse wheel)
    double        time = 0;        ///< seconds (Window::Time)
};

/// The edges of the screen around the window's content, dp (see platform/Android.h).
struct ScreenEdges {
    float left = 0, top = 0, right = 0, bottom = 0;
};

struct MobileState {
    bool phone = false;         ///< the phone layout is on this frame
    bool touchScreen = false;   ///< fingers are the main input (Android)

    // --- The drop cursor (ADR-0004, "Курсор-капля") ---
    bool   cursorIn3d = false;    ///< its button in the row under block 1, in the model
    bool   cursorInSketch = true; ///< the same in a sketch; drawing tools have it on regardless (author, 17.10)
    bool   cursorShown = false;   ///< drawn: it was called at least once since it was switched on
    ImVec2 cursorFinger{0, 0};    ///< where its round part is (under the finger)
    ImVec2 cursorTip{0, 0};       ///< where it points (what the mouse is for ImGui)
    ImVec2 cursorBefore{0, 0};    ///< before the touch that moved it (put back when that touch was a turn)
    bool   cursorDragging = false;   ///< a finger leads it now

    // --- Selection with a frame (the row under block 1) ---
    bool   boxMode = false;
    bool   boxHeld = false;   ///< with the cursor: the first corner is set (OK once), the button is held

    // --- Pressed in the row under block 1 this frame, run by the touch input next frame ---
    bool okPressed = false;       ///< a click at the cursor's tip (cursor on) or "confirm" (off)
    bool okLongPressed = false;   ///< confirm the operation, cursor or not
    bool cancelPressed = false;

    // --- This frame's gestures over the view (applied by the viewport) ---
    GestureStep gestures;

    // --- Block 1 (tree / tool / dimensions) and block 2 ---
    int  block1Tab = 0;          ///< 0 tree, 1 the tool's panel, 2 dimensions (variables)
    bool hadToolPanel = false;   ///< a tool's panel was there last frame (a new one switches to it)
    std::vector<std::string> collapsedGroups;   ///< block 2's groups folded by the user (captions)
    bool tabsFolded = false;     ///< block 3 folded to its arrow
    float stripScroll = 0;       ///< block 4 scrolled sideways

    // --- Layout of the last frame ---
    ImVec2 viewMin{0, 0}, viewMax{0, 0};   ///< the 3D view
    ScreenEdges insets;                    ///< safe area around the content (cut-out, bars, corners)
    int    cutoutSide = 0;                 ///< -1 the camera is on the left, 1 on the right, 0 none
    float  cutoutFrom = 0, cutoutTo = 0;   ///< where along that side (dp)
    float  cornerRadius = 0;
    float  keyboardHeight = 0;             ///< the system keyboard (dp), 0 when hidden
    bool   hardKeyboard = false;

    // --- Value input: the numpad's keys, laid out this frame (mobile/Numpad) ---
    struct NumpadKey {
        ImVec2 min, max;
        int    code = 0;   ///< a character, or one of the Numpad* codes below
    };
    std::vector<NumpadKey> numpadKeys;
    ImVec2 numpadMin{0, 0}, numpadMax{0, 0};   ///< the whole plate: a touch between keys goes nowhere
    int numpadDown = -1;   ///< the key under a finger now (drawn pressed)

    // --- Value input ---
    bool        numpad = false;        ///< the numpad is up in block 1 (a field is being typed in)
    bool        fullKeyboard = false;  ///< the system keyboard was asked for instead (⌨)
    std::string previousValue;         ///< the value last confirmed in a field ("пред.")
    std::string editingText;           ///< what the active field holds now (shown above the numpad)

    // --- "Скрыть управление" (the hotbar): only the view, the logo and the hotbar ---
    bool controlsHidden = false;

    // --- The layout editor and the first-start setup (docs/dev/mockups 2a–2d, 5a–5b) ---
    enum class Part : unsigned char { None, Block1, Block2, Undo, Tabs, Cube, Hotbar, Strip };
    bool layoutEditor = false;   ///< full screen: choose a control, change it
    Part editing = Part::None;
    int  setupStep = 0;          ///< first start: 1 the work layer, 2 navigation (then the layout editor); 0 none
    bool setupOpenedDoc = false; ///< the setup opened the example part (closed again at the end)
    struct Rect {
        ImVec2 min{0, 0}, max{0, 0};
        bool   Contains(ImVec2 p) const { return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y; }
    };
    Rect rBlock1, rBlock2, rUndo, rTabs, rCube, rHotbar, rStrip, rView;   ///< last frame's places

    // --- Block 2, compact: a group's strip over the view ---
    struct FlyItem {
        std::string id, label, icon;
        bool        active = false;
    };
    std::string          flyGroup;     ///< the group whose strip is open (empty: none)
    std::vector<FlyItem> flyItems;     ///< its commands (filled while block 2 is drawn)
    float                flyY = 0;     ///< the group's row (the strip opens there)
    float                flyScroll = 0;

    // --- The open tool's choices (features/ToolChoice.h): the top islands ---
    struct ChoiceGroup {
        std::string id, title;
        std::vector<std::pair<std::string, std::string>> items;   ///< icon (may be empty: text), label
        int current = 0;
        int disabled = 0;      ///< bit i: item i cannot be chosen now
        bool lineType = false; ///< the sketch's line type: an island of its own
    };
    std::vector<ChoiceGroup> choices;       ///< filled while this frame's tool panel is drawn
    std::vector<ChoiceGroup> lastChoices;   ///< the last non-empty set (the panel may be on another tab)
    std::string choicesOwner;               ///< whose they are (the tool's title)
    std::string pickedChoice;               ///< a choice tapped in an island: "id"…
    int         pickedIndex = -1;           ///< …and the item, taken by ToolChoice next frame
    float       choiceScroll = 0;           ///< the tool island scrolled sideways

    // --- View cube ---
    bool cubeOpen = false;   ///< the big cube panel is up

    // --- Hotbar at the camera ---
    bool hotbarPicking = false;   ///< "+": the list of commands to add is open

    // --- The last run ended in a crash or a hang (layer 3 of ADR-0004) ---
    struct LastCrash {
        bool        open = false;
        bool        hang = false;
        std::string code;     ///< core/CrashCode
        std::string reason;   ///< what the system says ("native crash", "anr", …)
        std::string report;   ///< the report's text
        std::string file;     ///< its file (UTF-8), to share
    } lastCrash;

    // --- Stylus (ADR-0004, "Стилус") ---
    bool   penNear = false;      ///< hovering or touching: fingers only navigate (palm rejection)
    double penSeenAt = -10.0;

    /// Frames the touch input still needs drawn (clicks it queued in ImGui).
    int framesWanted = 0;
};

} // namespace bz::app::mobile
