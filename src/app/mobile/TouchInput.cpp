#include "app/mobile/TouchInput.h"

#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "app/mobile/MobileLayout.h"
#include "app/sketch/SketchEditor.h"
#include "core/Log.h"
#include "core/Monitor.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace bz::app::mobile {
namespace {

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

/// Whether the panel at `p` (the topmost window there) can scroll up and
/// down. One that cannot (the big cube, a small island) takes a drag in any
/// direction as a drag: an upward pull on the cube turned nothing before.
bool ScrollsAt(ImVec2 p) {
    const ImGuiContext& g = *GImGui;
    for (int i = g.Windows.Size - 1; i >= 0; --i) {
        const ImGuiWindow* w = g.Windows[i];
        if (!w->WasActive || w->Hidden || !w->Rect().Contains(p) || (w->Flags & ImGuiWindowFlags_Tooltip)) continue;
        // A child list inside: its own scroll counts.
        for (const ImGuiWindow* c = w; c; c = c->ParentWindow)
            if (c->ScrollMax.y > 0.0f) return true;
        // The window's children (lists) under the point.
        for (int k = i + 1; k < g.Windows.Size; ++k) {
            const ImGuiWindow* c = g.Windows[k];
            if (c->RootWindow == w && c->WasActive && !c->Hidden && c->Rect().Contains(p) && c->ScrollMax.y > 0.0f) return true;
        }
        return false;
    }
    return true;
}

ImVec2 TipOf(const MobileState& m, ImVec2 finger, float reach) {
    ImVec2 tip{finger.x, finger.y - reach};
    // Its invisible hit box never leaves the work area.
    tip.x = std::clamp(tip.x, m.viewMin.x + 1.0f, std::max(m.viewMin.x + 1.0f, m.viewMax.x - 2.0f));
    tip.y = std::clamp(tip.y, m.viewMin.y + 1.0f, std::max(m.viewMin.y + 1.0f, m.viewMax.y - 2.0f));
    return tip;
}

} // namespace

float CursorReach(const AppContext& app) {
    return static_cast<float>(app.settings.mobile.cursorReach * app.settings.mobile.labelScale) * app.ui.scale;
}

bool CursorActive(const AppContext& app) {
    const MobileState& m = app.shell.mobile;
    if (!m.phone) return false;
    const Session* s = app.workspace.Active();
    if (!s || app.workspace.ShowsHome()) return false;
    if (s->mode == EditMode::Sketch) return m.cursorInSketch || SketchToolNeedsCursor(app);
    return m.cursorIn3d;
}

bool SketchToolNeedsCursor(const AppContext& app) {
    switch (app.sketch.Tool()) {
        case SketchTool::Select:
        case SketchTool::Dimension:
        case SketchTool::Project:
        case SketchTool::Trim:
        case SketchTool::Extend:
        case SketchTool::Split:
        case SketchTool::Fillet:
        case SketchTool::Coincident:
        case SketchTool::Horizontal:
        case SketchTool::Vertical:
        case SketchTool::Parallel:
        case SketchTool::Perpendicular:
        case SketchTool::Equal:
        case SketchTool::Tangent:
        case SketchTool::Fix:
        case SketchTool::Midpoint:
        case SketchTool::Concentric:
        case SketchTool::Symmetric: return false;
        default: return true;
    }
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

void TouchInput::Wheel(float dx, float dy) {
    // One wheel step scrolls about five lines of text in ImGui.
    const float step = 5.0f * ImGui::GetTextLineHeightWithSpacing() / std::max(0.1f, myScrollSpeed);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMouseWheelEvent(dx / step, dy / step);
    myQueued = std::max(myQueued, 2);
}

void TouchInput::Fling(double now) {
    if (myFlingX == 0 && myFlingY == 0) return;
    if (myUi.id >= 0) return;   // still under the finger
    const double dt = std::clamp(now - myFlingTime, 0.0, 0.05);
    myFlingTime = now;
    Pos(myFlingAt);
    Wheel(static_cast<float>(myFlingX * dt), static_cast<float>(myFlingY * dt));
    const float decay = static_cast<float>(std::exp(-dt * 3.5));   // about a second to stop
    myFlingX *= decay;
    myFlingY *= decay;
    if (std::hypot(myFlingX, myFlingY) < 40.0f) myFlingX = myFlingY = 0;
}

bool TouchInput::Busy() const {
    return !myRoutes.empty() || myQueued > 0 || myGestures.Active() || myUi.id >= 0 || myNumpadId >= 0 || myFlingX != 0 || myFlingY != 0;
}

void TouchInput::BeginFrame(AppContext& app, double now) {
    MobileState& m = app.shell.mobile;
    if (myQueued > 0) --myQueued;
    if (m.penNear && myPenId < 0 && now - m.penSeenAt > kPenAway) m.penNear = false;
    const MobileSettings& tune = app.settings.mobile;
    const bool cursor = CursorActive(app);
    DragMap drags = (cursor ? tune.gesturesOn : tune.gesturesOff).drags;
    // A sketch lies flat to the eye: one finger moves it rather than tilting it into 3D
    // (the author, 17.10: the sketch turned under the finger with the cursor off).
    if (const Session* s = app.workspace.Active(); s && s->mode == EditMode::Sketch && !cursor && drags.one == FingerDrag::Orbit)
        drags.one = FingerDrag::Pan;
    myGestures.SetMode({cursor, m.boxMode && !cursor, m.penNear, drags});
    mySlop = static_cast<float>(tune.tapSlop);
    myScrollSpeed = static_cast<float>(tune.scrollSpeed);
    myGestures.SetTuning({static_cast<double>(tune.tapSlop), tune.longPress});
    Fling(now);

    for (const PointerEvent& e : myQueue) Route1(app, e);
    myQueue.clear();

    // A finger held still on a panel: the right button (tree rows, lists).
    if (myUi.id >= 0 && myUi.mode == UiFinger::Mode::Undecided && now - myUi.since >= app.settings.mobile.longPress) {
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
    // The tip stays in the view when the view moves under it (the numpad widens
    // block 1, the phone turned over): ✓ clicked a panel otherwise.
    if (m.cursorShown && !m.cursorDragging && m.viewMax.x > m.viewMin.x) {
        const ImVec2 tip = TipOf(m, {m.cursorTip.x, m.cursorTip.y + CursorReach(app)}, CursorReach(app));
        if (tip.x != m.cursorTip.x || tip.y != m.cursorTip.y) {
            m.cursorFinger = {m.cursorFinger.x + tip.x - m.cursorTip.x, m.cursorFinger.y + tip.y - m.cursorTip.y};
            m.cursorTip = tip;
        }
    }
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
        // Between the keys: the panel drawn under the numpad must not get it.
        if (Inside(at, m.numpadMin, m.numpadMax)) {
            myRoutes[e.id] = Route::Ignored;
            return;
        }
    }
    if (e.action == PointerAction::Down) {
        myFlingX = myFlingY = 0;   // a finger stops a flung list
        Route route = Route::View;
        if (UiAt(at) || !Inside(at, m.viewMin, m.viewMax)) route = myUi.id < 0 ? Route::Ui : Route::Ignored;
        myRoutes[e.id] = route;
        // One line per touch: where it went (phone reports, the emulator's check).
        log::Info("Touch: finger at {:.0f},{:.0f} -> {}", at.x, at.y, route == Route::Ui ? "panel" : route == Route::View ? "view" : "ignored");
        if (route == Route::Ui) {
            myUi = {e.id, at, at, e.time, UiFinger::Mode::Undecided, !ScrollsAt(at)};
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
    if (e.action == PointerAction::Move && myUi.mode != UiFinger::Mode::Scroll) myFlingX = myFlingY = 0;
    switch (e.action) {
        case PointerAction::Move: {
            const float dx = at.x - myUi.start.x, dy = at.y - myUi.start.y;
            if (myUi.mode == UiFinger::Mode::Undecided && std::hypot(dx, dy) > mySlop) {
                // Up or down scrolls the list under the finger; sideways is a
                // drag (sliders, the projects strip, a value dragged).
                if (std::abs(dy) >= std::abs(dx) && !myUi.dragOnly) {
                    myUi.mode = UiFinger::Mode::Scroll;
                } else {
                    myUi.mode = UiFinger::Mode::Drag;
                    Pos(myUi.start);
                    Button(ImGuiMouseButton_Left, true);
                }
            }
            if (myUi.mode == UiFinger::Mode::Scroll) {
                Wheel(at.x - last.x, at.y - last.y);
                // The finger's speed, smoothed: what a fling carries on with.
                const double dt = std::max(1e-3, e.time - myLastMoveTime);
                if (e.time - myLastMoveTime < 0.1) {
                    myFlingX = 0.6f * myFlingX + 0.4f * static_cast<float>((at.x - last.x) / dt);
                    myFlingY = 0.6f * myFlingY + 0.4f * static_cast<float>((at.y - last.y) / dt);
                } else {
                    myFlingX = myFlingY = 0;
                }
            } else if (myUi.mode == UiFinger::Mode::Drag) {
                Pos(at);
            }
            myLastMoveTime = e.time;
            break;
        }
        case PointerAction::Up:
            if (myUi.mode == UiFinger::Mode::Scroll && e.time - myLastMoveTime < 0.08 && std::hypot(myFlingX, myFlingY) > 150.0f) {
                myFlingAt = at;   // the list keeps going
                myFlingTime = e.time;
            } else {
                myFlingX = myFlingY = 0;
            }
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
                // The eraser deletes what it touches: picked as it touches, deleted as it
                // lifts (a sketch picks on the release — a Delete at the touch took the
                // selection from before).
                Button(ImGuiMouseButton_Left, true, ImGuiMouseSource_Pen);
            } else if (!myPenButton) {
                Button(ImGuiMouseButton_Left, true, ImGuiMouseSource_Pen);
            }
            break;
        case PointerAction::Move:
            if (e.id != myPenId) break;
            if (myPenButton && !myPenFraming && std::hypot(at.x - myPenStart.x, at.y - myPenStart.y) > mySlop) {
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
            if (e.tool == PointerTool::Eraser) {
                Button(ImGuiMouseButton_Left, false, ImGuiMouseSource_Pen);
                if (e.action == PointerAction::Up) app.Later([&app] { app.actions.Run("edit.delete"); });
            } else {
                if (myPenButton && !myPenFraming && e.action == PointerAction::Up) {
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

    const float reach = CursorReach(app);
    const float speed = static_cast<float>(app.settings.mobile.cursorSpeed);
    if (step.cursorStarted) {
        m.cursorBefore = m.cursorShown ? m.cursorTip : ImVec2{-1, -1};
        // The precise cursor: with a tip already shown it does not jump under
        // the finger; it moves a part of the finger's travel from where it is.
        myRelative = speed < 0.99f && m.cursorShown && step.cursorFinger;
        if (myRelative) {
            myAnchorFinger = ToIm(*step.cursorFinger);
            myAnchorTip = m.cursorTip;
            myRelativeTravel = 0;
            myRelativeSince = ImGui::GetTime();
        }
        m.cursorShown = true;
        m.cursorDragging = true;
    }
    if (step.cursorFinger) {
        const ImVec2 finger = ToIm(*step.cursorFinger);
        if (myRelative) {
            myRelativeTravel = std::max(myRelativeTravel, std::hypot(finger.x - myAnchorFinger.x, finger.y - myAnchorFinger.y));
            const ImVec2 aim{myAnchorTip.x + (finger.x - myAnchorFinger.x) * speed, myAnchorTip.y + (finger.y - myAnchorFinger.y) * speed};
            m.cursorTip = TipOf(m, {aim.x, aim.y + reach}, reach);
            m.cursorFinger = {m.cursorTip.x, m.cursorTip.y + reach};   // the drop drawn from the tip, not at the finger
        } else {
            m.cursorFinger = finger;
            m.cursorTip = TipOf(m, m.cursorFinger, reach);
        }
    }
    if (step.cursorRevert) {
        m.cursorDragging = false;
        if (m.cursorBefore.x < 0) {
            m.cursorShown = false;
        } else {
            m.cursorTip = m.cursorBefore;
            m.cursorFinger = {m.cursorTip.x, m.cursorTip.y + reach};
        }
    }
    if (step.cursorEnded) {
        m.cursorDragging = false;
        // A tap with the precise cursor still puts the tip over the tapped spot.
        if (myRelative && myRelativeTravel <= mySlop && ImGui::GetTime() - myRelativeSince < app.settings.mobile.longPress) {
            m.cursorTip = TipOf(m, myAnchorFinger, reach);
            m.cursorFinger = {m.cursorTip.x, m.cursorTip.y + reach};
        }
        myRelative = false;
    }

    if (step.tap) {
        Click(ToIm(*step.tap), ImGuiMouseButton_Left);
        mon::Action("touch", "касание");
    }
    if (step.longPress) {
        const ImVec2 at = CursorActive(app) ? m.cursorTip : ToIm(*step.longPress);
        Click(at, ImGuiMouseButton_Right);   // the context menu
        mon::Action("touch", "зажатие");
    }
    // Two- and three-finger taps: as the user set them for this cursor state.
    const FingerGestures& set = CursorActive(app) ? app.settings.mobile.gesturesOn : app.settings.mobile.gesturesOff;
    const auto runTap = [&app](FingerTap t) {
        const char* id = t == FingerTap::Undo ? "edit.undo" : t == FingerTap::Redo ? "edit.redo" : t == FingerTap::Fit ? "view.fit"
                       : t == FingerTap::Home ? "view.home" : nullptr;
        if (id) app.Later([&app, id] { app.actions.Run(id); });
    };
    if (step.tap2) runTap(set.tap2);
    if (step.tap3) runTap(set.tap3);
    if (step.boxFrom) {
        Pos(ToIm(*step.boxFrom));
        Button(ImGuiMouseButton_Left, true);
    }
    if (step.boxTo) Pos(ToIm(*step.boxTo));
    if (step.boxEnded) {
        Button(ImGuiMouseButton_Left, false);
        m.boxMode = false;   // one frame, then the finger turns the view again (a mode left on looked like "rotation is broken")
    }
    m.gestures = step;
}

void TouchInput::RunRowButtons(AppContext& app) {
    MobileState& m = app.shell.mobile;
    const bool cursor = CursorActive(app) && m.cursorShown;
    if (m.okPressed) {
        m.okPressed = false;
        if (ImGui::GetIO().WantTextInput) {
            // A value being typed: ✓ enters it (as ↵), never a click at the tip —
            // that click landed off the field and the value was lost (17.10, the
            // circle's 20 mm). The next ✓ goes on as usual.
            ImGuiIO& io = ImGui::GetIO();
            if (!m.editingText.empty()) m.previousValue = m.editingText;
            io.AddKeyEvent(ImGuiKey_Enter, true);
            io.AddKeyEvent(ImGuiKey_Enter, false);
            myQueued = std::max(myQueued, 3);
        } else if (cursor && m.boxMode) {
            // A frame with the cursor: OK at one corner, OK at the other.
            Pos(m.cursorTip);
            Button(ImGuiMouseButton_Left, !m.boxHeld);
            m.boxHeld = !m.boxHeld;
            if (!m.boxHeld) m.boxMode = false;   // the second corner: done, one frame per press of the button
        } else if (cursor) {
            // With the cursor ✓ is its click, even when the operation could be made:
            // the next contour, edge or face is chosen with it too (18.10.2026: a green
            // ✓ applied instead, the second edge could not be picked). A long press is «Готово».
            Click(m.cursorTip, ImGuiMouseButton_Left);
        } else if (OperationEditor* op = app.ops.Active(); op && app.Doc() && op->HasApply() && op->Ready(*app.Doc())) {
            // Everything needed is chosen (✓ is green): it makes the feature and the
            // operation stays open for the next one (the author, 18.10.2026). A long
            // press is «Готово» (commit and close).
            app.Later([&app, op] {
                if (app.ops.Active() == op && app.Doc()) op->Apply(app, *app.Doc());
            });
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
        // A field being typed in or a menu on top takes it as the Esc key. Otherwise
        // it is the command: the key alone reached nobody (shortcuts on the phone come
        // as chords, like "Назад"), so ✕ did nothing in a sketch.
        const bool keyTakers = io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (keyTakers) {
            io.AddKeyEvent(ImGuiKey_Escape, true);
            io.AddKeyEvent(ImGuiKey_Escape, false);
            if (!io.WantTextInput) {   // a context menu: closed as HandleShortcuts does for Esc
                ImGuiContext& g = *GImGui;
                if (!g.OpenPopupStack.empty()) {
                    const ImGuiWindow* top = g.OpenPopupStack.back().Window;
                    if (!top || !(top->Flags & ImGuiWindowFlags_Modal)) ImGui::ClosePopupToLevel(g.OpenPopupStack.Size - 1, true);
                }
            }
        } else {
            mon::Action("touch", "отмена");
            app.Later([&app] { CancelPressed(app); });
        }
        myQueued = std::max(myQueued, 3);
    }
}

void TouchInput::PlaceMouse(AppContext& app) {
    MobileState& m = app.shell.mobile;
    if (myUi.id >= 0 || myPenId >= 0 || myMouseButtons != 0 || myMouseInUse) return;   // a finger on a panel, the pen or a mouse has it
    if (myFlingX != 0 || myFlingY != 0) return;                                        // a flung list scrolls under the last spot
    if (m.penNear) return;                                               // the hovering pen points
    if (myGestures.GetMode().boxMode && myGestures.Active()) return;     // a frame drawn with a finger
    if (CursorActive(app) && m.cursorShown) {
        Pos(m.cursorTip);
    } else if (myQueued == 0 && !myGestures.Active()) {
        Pos({-FLT_MAX, -FLT_MAX});   // nothing points: no highlight left under the last tap
    }
}

} // namespace bz::app::mobile
