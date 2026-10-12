// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 MikeVerx
#include "app/viewport/TouchGestures.h"

#include "core/Monitor.h"

#include <cmath>
#include <numbers>
#include <optional>

namespace bz::app {
namespace {

enum class Kind : int { Pan, Orbit, Zoom, Roll };

/// A gesture pauses this long before the next movement counts as a new one.
constexpr double kNewGestureAfter = 0.3;   // s

/// The one modifier key held now; none held is None, two at once nothing.
std::optional<ModifierChoice> HeldModifier() {
    const ImGuiIO& io = ImGui::GetIO();
    const int held = (io.KeyShift ? 1 : 0) + (io.KeyCtrl ? 1 : 0) + (io.KeyAlt ? 1 : 0);
    if (held > 1) return std::nullopt;
    if (io.KeyShift) return ModifierChoice::Shift;
    if (io.KeyCtrl) return ModifierChoice::Ctrl;   // Cmd on macOS (ImGui swaps them there)
    if (io.KeyAlt) return ModifierChoice::Alt;
    return ModifierChoice::None;
}

const char* KindName(Kind k) {
    switch (k) {
        case Kind::Pan:   return "тачпад: сдвиг";
        case Kind::Orbit: return "тачпад: вращение";
        case Kind::Zoom:  return "тачпад: масштаб";
        case Kind::Roll:  return "тачпад: крен";
    }
    return "тачпад";
}

void RunCommand(AppContext& app, const ViewportFrame& vf, const std::string& id) {
    if (id.empty()) return;
    mon::Action("view", "тачпад: " + id);
    if (id == kTouchMenu) {
        ChooseOrbitPivot(app, vf, ImGui::GetMousePos());   // the menu offers a sketch on the face there
        ImGui::OpenPopup("##viewport-menu");
        return;
    }
    app.Later([&app, id] { app.actions.Run(id); });
}

} // namespace

bool NavigateTouch(AppContext& app, const ViewportFrame& vf, bool sceneHovered) {
    ShellState::Navigation& nav = app.shell.nav;
    const TouchNavigation& t = app.settings.touch;
    const ImVec2 mouse = ImGui::GetMousePos();
    const double sign = t.invert ? -1.0 : 1.0;
    // A new gesture (after a pause, or another kind) turns about the point
    // under the fingers' cursor and is one action in the monitor's log.
    const auto begin = [&](Kind kind) {
        if (app.now - nav.touchAt > kNewGestureAfter || nav.touchKind != static_cast<int>(kind)) {
            if (kind == Kind::Orbit || kind == Kind::Roll) ChooseOrbitPivot(app, vf, mouse);
            mon::Action("view", KindName(kind));
        }
        nav.touchAt = app.now;
        nav.touchKind = static_cast<int>(kind);
    };

    bool wheelTaken = false;
    for (const platform::TouchEvent& e : app.touch) {
        switch (e.gesture) {
            case platform::TouchGesture::Scroll: {
                wheelTaken = true;
                if (!sceneHovered) break;
                const std::optional<ModifierChoice> modifier = HeldModifier();
                const TouchMove move = modifier ? t.MoveWith(*modifier) : TouchMove::None;
                // Gliding after the fingers left only pans: a turn or a zoom stops with the fingers.
                if (move == TouchMove::None || (e.momentum && move != TouchMove::Pan)) break;
                const double dx = e.dx * t.speed * sign, dy = e.dy * t.speed * sign;
                if (move == TouchMove::Pan) {
                    begin(Kind::Pan);
                    app.viewport.Pan(dx, dy);
                } else if (move == TouchMove::Orbit) {
                    begin(Kind::Orbit);
                    const double k = kOrbitPerPixel * app.settings.orbitSpeed;
                    app.viewport.Orbit(dx * k, dy * k * (app.settings.invertOrbit ? -1.0 : 1.0), nav.pivot);
                } else {
                    begin(Kind::Zoom);
                    // Fingers down (the content goes down) bring it closer, as a wheel turned forward.
                    app.viewport.ZoomAt(std::pow(1.003, dy), vf.ToLocal(mouse));
                }
                break;
            }
            case platform::TouchGesture::Pinch:
                wheelTaken = true;
                if (!sceneHovered || !t.pinchZooms) break;
                begin(Kind::Zoom);
                app.viewport.ZoomAt(std::exp(static_cast<double>(e.amount) * t.speed), vf.ToLocal(mouse));
                break;
            case platform::TouchGesture::Rotate: {
                if (!sceneHovered || t.turn == TouchTurn::None) break;
                const double angle = static_cast<double>(e.amount) * std::numbers::pi / 180.0 * t.speed * sign;
                if (t.turn == TouchTurn::Roll) {
                    begin(Kind::Roll);
                    app.viewport.Roll(angle, nav.pivot);
                } else {
                    begin(Kind::Orbit);
                    app.viewport.Orbit(-angle, 0.0, nav.pivot);   // fingers counter-clockwise: the model too, seen from above
                }
                break;
            }
            case platform::TouchGesture::SmartZoom:
                if (sceneHovered) RunCommand(app, vf, t.doubleTap);
                break;
            case platform::TouchGesture::ForceClick:
                if (sceneHovered) RunCommand(app, vf, t.forceClick);
                break;
        }
    }
    return wheelTaken;
}

} // namespace bz::app
