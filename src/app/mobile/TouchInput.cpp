#include "app/mobile/TouchInput.h"

#include "app/Operations.h"
#include "app/mobile/MobileLayout.h"
#include "core/Monitor.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace bz::app::mobile {
namespace {

constexpr float  kSlop = static_cast<float>(kTapSlopDp);
constexpr double kPenAway = 1.0;   ///< seconds after the last pen event: fingers do everything again

Vec2   ToVec(ImVec2 p) { return {p.x, p.y}; }
ImVec2 ToIm(Vec2 p) { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }

bool Inside(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.y >= a.y && p.x < b.x && p.y < b.y; }

/// Whether a panel, island, menu or dialog is at `p` (the last frame's
/// windows): anything but the root window and the 3D view.
bool UiAt(ImVec2 p) {
    const ImGuiContext& g = *GImGui;
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return true;   // a menu or a dialog takes every touch
    for (const ImGuiWindow* w : g.Windows) {
        if (!w->WasActive || w->Hidden || !w->Rect().Contains(p)) continue;
        if (w->Flags & ImGuiWindowFlags_Tooltip) continue;
        const char* name = w->Name;
        if (std::strcmp(name, "##root") == 0) continue;
        if (std::strstr(name, "##viewport")) continue;   // the view and what is drawn inside it
        return true;
    }
    return false;
}

ImVec2 TipOf(const MobileState& m, ImVec2 finger, float reach) {
    ImVec2 tip{finger.x, finger.y - reach};
    // Its invisible hit box never leaves the work area.
    tip.x = std::clamp(tip.x, m.viewMin.x + 1.0f, std::max(m.viewMin.x + 1.0f, m.viewMax.x - 2.0f));
    tip.y = std::clamp(tip.y, m.viewMin.y + 1.0f, std::max(m.viewMin.y + 1.0f, m.viewMax.y - 2.0f));
    return tip;
}

} // namespace

bool CursorActive(const AppContext& app) {
    const MobileState& m = app.shell.mobile;
    if (!m.phone) return false;
    const Session* s = app.workspace.Active();
    if (!s || app.workspace.ShowsHome()) return false;
    return s->mode == EditMode::Sketch || m.cursorIn3d;
}

void TouchInput::Pos(ImVec2 at, ImGuiMouseSource source) {
    if (myMouseKnown && at.x == myMouse.x && at.y == myMouse.y) return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(source);
    io.AddMousePosEvent(at.x, at.y);
    myMouse = at;
    myMouseKnown = true;
}

void TouchInput::Button(int button, bool down, ImGuiMouseSource source) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(source);
    io.AddMouseButtonEvent(button, down);
    myQueued = std::max(myQueued, 4);
}

void TouchInput::Click(ImVec2 at, int button) {
    Pos(at);
    Button(button, true);
    Button(button, false);
}

bool TouchInput::Busy() const { return !myRoutes.empty() || myQueued > 0 || myGestures.Active() || myUi.id >= 0 || myNumpadId >= 0; }

void TouchInput::BeginFrame(AppContext& app, double now) {
    MobileState& m = app.shell.mobile;
    if (myQueued > 0) --myQueued;
    if (m.penNear && myPenId < 0 && now - m.penSeenAt > kPenAway) m.penNear = false;
    myGestures.SetMode({CursorActive(app), m.boxMode && !CursorActive(app), m.penNear});

    for (const PointerEvent& e : myQueue) Route1(app, e);
    myQueue.clear();

    // A finger held still on a panel: the right button (tree rows, lists).
    if (myUi.id >= 0 && myUi.mode == UiFinger::Mode::Undecided && now - myUi.since >= kLongPressSeconds) {
        myUi.mode = UiFinger::Mode::Held;
        Click(myUi.at, ImGuiMouseButton_Right);
    }
    // A held backspace repeats.
    if (myNumpadId >= 0 && m.numpadDown >= 0 && m.numpadDown < static_cast<int>(m.numpadKeys.size()) &&
        m.numpadKeys[static_cast<std::size_t>(m.numpadDown)].code == NumpadBackspace && now - myNumpadSince > 0.45 &&
        now - myNumpadRepeat > 0.06) {
        myNumpadRepeat = now;
        FireNumpadKey(app, NumpadBackspace);
    }
    myGestures.Tick(now);
    ApplyGestures(app);
    RunRowButtons(app);
    PlaceMouse(app);
    m.framesWanted = Busy() ? 2 : 0;
}

void TouchInput::Route1(AppContext& app, const PointerEvent& e) {
    MobileState& m = app.shell.mobile;
    const ImVec2 at{e.x, e.y};
    if (e.tool == PointerTool::Mouse) {
        MouseEvent(e);
        return;
    }
    if (e.tool == PointerTool::Stylus || e.tool == PointerTool::Eraser) {
        m.penNear = e.action != PointerAction::HoverExit;
        m.penSeenAt = e.time;
        PenEvent(app, e);
        return;
    }
    // A finger.
    myMouseInUse = false;
    if (e.action == PointerAction::Down && m.numpad && myNumpadId < 0) {
        for (std::size_t i = 0; i < m.numpadKeys.size(); ++i) {
            if (!Inside(at, m.numpadKeys[i].min, m.numpadKeys[i].max)) continue;
            myRoutes[e.id] = Route::Numpad;
            myNumpadId = e.id;
            myNumpadSince = myNumpadRepeat = e.time;
            m.numpadDown = static_cast<int>(i);
            myQueued = std::max(myQueued, 2);
            return;
        }
    }
    if (e.action == PointerAction::Down) {
        Route route = Route::View;
        if (UiAt(at) || !Inside(at, m.viewMin, m.viewMax)) route = myUi.id < 0 ? Route::Ui : Route::Ignored;
        myRoutes[e.id] = route;
        if (route == Route::Ui) {
            myUi = {e.id, at, at, e.time, UiFinger::Mode::Undecided};
            Pos(at);   // hover first: the button goes down on a tap or a sideways drag
        } else if (route == Route::View) {
            myGestures.Down(e.id, ToVec(at), e.time);
        }
        return;
    }
    const auto it = myRoutes.find(e.id);
    if (it == myRoutes.end()) return;
    const Route route = it->second;
    if (route == Route::Numpad) {
        if (e.action == PointerAction::Up && m.numpadDown >= 0 && m.numpadDown < static_cast<int>(m.numpadKeys.size())) {
            const MobileState::NumpadKey& key = m.numpadKeys[static_cast<std::size_t>(m.numpadDown)];
            // Held backspace already repeated: no extra one on release.
            const bool repeated = key.code == NumpadBackspace && e.time - myNumpadSince > 0.45;
            if (Inside(at, key.min, key.max) && !repeated) FireNumpadKey(app, key.code);
        }
        if (e.action == PointerAction::Up || e.action == PointerAction::Cancel) {
            m.numpadDown = -1;
            myNumpadId = -1;
            myQueued = std::max(myQueued, 2);
        }
    } else if (route == Route::Ui) {
        UiEvent(e);
    } else if (route == Route::View) {
        if (e.action == PointerAction::Move) myGestures.Move(e.id, ToVec(at), e.time);
        else if (e.action == PointerAction::Up) myGestures.Up(e.id, ToVec(at), e.time);
        else if (e.action == PointerAction::Cancel) myGestures.Cancel();
    }
    if (e.action == PointerAction::Up || e.action == PointerAction::Cancel) myRoutes.erase(e.id);
}

void TouchInput::UiEvent(const PointerEvent& e) {
    const ImVec2 at{e.x, e.y};
    const ImVec2 last = myUi.at;
    myUi.at = at;
    ImGuiIO& io = ImGui::GetIO();
    switch (e.action) {
        case PointerAction::Move: {
            const float dx = at.x - myUi.start.x, dy = at.y - myUi.start.y;
            if (myUi.mode == UiFinger::Mode::Undecided && std::hypot(dx, dy) > kSlop) {
                // Up or down scrolls the list under the finger; sideways is a
                // drag (sliders, the projects strip, a value dragged).
                if (std::abs(dy) >= std::abs(dx)) {
                    myUi.mode = UiFinger::Mode::Scroll;
                } else {
                    myUi.mode = UiFinger::Mode::Drag;
                    Pos(myUi.start);
                    Button(ImGuiMouseButton_Left, true);
                }
            }
            if (myUi.mode == UiFinger::Mode::Scroll) {
                // One wheel step is about five lines of text in ImGui.
                const float lines = (at.y - last.y) / (5.0f * ImGui::GetTextLineHeightWithSpacing());
                const float sideways = (at.x - last.x) / (5.0f * ImGui::GetTextLineHeightWithSpacing());
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMouseWheelEvent(sideways, lines);
                myQueued = std::max(myQueued, 2);
            } else if (myUi.mode == UiFinger::Mode::Drag) {
                Pos(at);
            }
            break;
        }
        case PointerAction::Up:
            if (myUi.mode == UiFinger::Mode::Undecided) Click(myUi.start, ImGuiMouseButton_Left);
            else if (myUi.mode == UiFinger::Mode::Drag) {
                Pos(at);
                Button(ImGuiMouseButton_Left, false);
            }
            myUi.id = -1;
            break;
        case PointerAction::Cancel:
            if (myUi.mode == UiFinger::Mode::Drag) Button(ImGuiMouseButton_Left, false);
            myUi.id = -1;
            break;
        default: break;
    }
}

void TouchInput::PenEvent(AppContext& app, const PointerEvent& e) {
    const ImVec2 at{e.x, e.y};
    switch (e.action) {
        case PointerAction::HoverMove:
            if (myPenId < 0) Pos(at, ImGuiMouseSource_Pen);
            break;
        case PointerAction::HoverExit: break;
        case PointerAction::Down:
            myPenId = e.id;
            myPenStart = at;
            myPenFraming = false;
            myPenButton = (e.buttons & 2) != 0;
            Pos(at, ImGuiMouseSource_Pen);
            if (e.tool == PointerTool::Eraser) {
                // The eraser deletes what it touches: pick it, then Delete.
                Button(ImGuiMouseButton_Left, true, ImGuiMouseSource_Pen);
                Button(ImGuiMouseButton_Left, false, ImGuiMouseSource_Pen);
                app.Later([&app] { app.actions.Run("edit.delete"); });
            } else if (!myPenButton) {
                Button(ImGuiMouseButton_Left, true, ImGuiMouseSource_Pen);
            }
            break;
        case PointerAction::Move:
            if (e.id != myPenId) break;
            if (myPenButton && !myPenFraming && std::hypot(at.x - myPenStart.x, at.y - myPenStart.y) > kSlop) {
                // The button and a drag: a selection frame from where it touched.
                myPenFraming = true;
                Pos(myPenStart, ImGuiMouseSource_Pen);
                Button(ImGuiMouseButton_Left, true, ImGuiMouseSource_Pen);
            }
            Pos(at, ImGuiMouseSource_Pen);
            break;
        case PointerAction::Up:
        case PointerAction::Cancel:
            if (e.id != myPenId) break;
            Pos(at, ImGuiMouseSource_Pen);
            if (e.tool != PointerTool::Eraser) {
                if (myPenButton && !myPenFraming) {
                    Button(ImGuiMouseButton_Right, true, ImGuiMouseSource_Pen);   // the button and a touch: the right click
                    Button(ImGuiMouseButton_Right, false, ImGuiMouseSource_Pen);
                } else {
                    Button(ImGuiMouseButton_Left, false, ImGuiMouseSource_Pen);
                }
            }
            myPenId = -1;
            break;
        default: break;
    }
}

void TouchInput::MouseEvent(const PointerEvent& e) {
    ImGuiIO& io = ImGui::GetIO();
    myMouseInUse = true;
    Pos({e.x, e.y}, ImGuiMouseSource_Mouse);
    if (e.action == PointerAction::Scroll) {
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMouseWheelEvent(e.scrollX, e.scrollY);
        return;
    }
    const int buttons = e.action == PointerAction::Up || e.action == PointerAction::Cancel ? 0 : e.buttons;
    const int map[3] = {ImGuiMouseButton_Left, ImGuiMouseButton_Right, ImGuiMouseButton_Middle};
    for (int i = 0; i < 3; ++i) {
        const bool now = (buttons >> i) & 1, before = (myMouseButtons >> i) & 1;
        if (now != before) Button(map[i], now, ImGuiMouseSource_Mouse);
    }
    myMouseButtons = buttons;
}

void TouchInput::ApplyGestures(AppContext& app) {
    MobileState& m = app.shell.mobile;
    GestureStep step = myGestures.Take();

    if (step.cursorStarted) {
        m.cursorBefore = m.cursorShown ? m.cursorTip : ImVec2{-1, -1};
        m.cursorShown = true;
        m.cursorDragging = true;
    }
    if (step.cursorFinger) {
        m.cursorFinger = ToIm(*step.cursorFinger);
        m.cursorTip = TipOf(m, m.cursorFinger, kCursorReach * static_cast<float>(app.settings.mobile.labelScale));
    }
    if (step.cursorRevert) {
        m.cursorDragging = false;
        if (m.cursorBefore.x < 0) {
            m.cursorShown = false;
        } else {
            m.cursorTip = m.cursorBefore;
            m.cursorFinger = {m.cursorTip.x, m.cursorTip.y + kCursorReach * static_cast<float>(app.settings.mobile.labelScale)};
        }
    }
    if (step.cursorEnded) m.cursorDragging = false;

    if (step.tap) {
        Click(ToIm(*step.tap), ImGuiMouseButton_Left);
        mon::Action("touch", "касание");
    }
    if (step.longPress) {
        const ImVec2 at = CursorActive(app) ? m.cursorTip : ToIm(*step.longPress);
        Click(at, ImGuiMouseButton_Right);   // the context menu
        mon::Action("touch", "зажатие");
    }
    if (step.undo) app.Later([&app] { app.actions.Run("edit.undo"); });
    if (step.redo) app.Later([&app] { app.actions.Run("edit.redo"); });
    if (step.boxFrom) {
        Pos(ToIm(*step.boxFrom));
        Button(ImGuiMouseButton_Left, true);
    }
    if (step.boxTo) Pos(ToIm(*step.boxTo));
    if (step.boxEnded) Button(ImGuiMouseButton_Left, false);
    m.gestures = step;
}

void TouchInput::RunRowButtons(AppContext& app) {
    MobileState& m = app.shell.mobile;
    const bool cursor = CursorActive(app) && m.cursorShown;
    if (m.okPressed) {
        m.okPressed = false;
        if (cursor && m.boxMode) {
            // A frame with the cursor: OK at one corner, OK at the other.
            Pos(m.cursorTip);
            Button(ImGuiMouseButton_Left, !m.boxHeld);
            m.boxHeld = !m.boxHeld;
        } else if (cursor) {
            Click(m.cursorTip, ImGuiMouseButton_Left);
        } else {
            app.Later([&app] { EnterPressed(app); });
        }
        mon::Action("touch", "ОК");
    }
    if (m.okLongPressed) {
        m.okLongPressed = false;
        app.Later([&app] { EnterPressed(app); });
    }
    if (m.cancelPressed) {
        m.cancelPressed = false;
        if (m.boxHeld) {   // a frame half made: dropped
            Button(ImGuiMouseButton_Left, false);
            m.boxHeld = false;
        }
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Escape, true);
        io.AddKeyEvent(ImGuiKey_Escape, false);
        myQueued = std::max(myQueued, 3);
    }
}

void TouchInput::PlaceMouse(AppContext& app) {
    MobileState& m = app.shell.mobile;
    if (myUi.id >= 0 || myPenId >= 0 || myMouseButtons != 0 || myMouseInUse) return;   // a finger on a panel, the pen or a mouse has it
    if (m.penNear) return;                                               // the hovering pen points
    if (myGestures.GetMode().boxMode && myGestures.Active()) return;     // a frame drawn with a finger
    if (CursorActive(app) && m.cursorShown) {
        Pos(m.cursorTip);
    } else if (myQueued == 0 && !myGestures.Active()) {
        Pos({-FLT_MAX, -FLT_MAX});   // nothing points: no highlight left under the last tap
    }
}

} // namespace bz::app::mobile
