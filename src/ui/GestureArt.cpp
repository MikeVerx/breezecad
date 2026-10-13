// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 MikeVerx
#include "ui/GestureArt.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bz::ui {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

/// 0 → 1 → 0 smoothly over one loop.
float BackAndForth(float phase) { return 0.5f - 0.5f * std::cos(2.0f * kPi * phase); }

float Phase(double time, double period) {
    const double p = std::fmod(time, period) / period;
    return static_cast<float>(p < 0 ? p + 1 : p);
}

ImVec2 Plus(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
ImVec2 Times(ImVec2 a, float k) { return {a.x * k, a.y * k}; }

struct Pad {
    ImVec2 min, max, centre;
    float  w = 0, h = 0, finger = 0;
};

/// Fingers where `at(phase)` puts them now, and a fading trail behind.
template <class Where>
void Fingers(ImDrawList* dl, const Pad& pad, float phase, Color colour, bool trail, Where at) {
    if (trail)
        for (int i = 3; i >= 1; --i) {
            const float back = phase - 0.035f * static_cast<float>(i);
            const ImU32 ghost = ToU32(colour.WithAlpha(0.10f + 0.05f * static_cast<float>(3 - i)));
            at(back < 0 ? back + 1 : back, [&](ImVec2 p) { dl->AddCircleFilled(p, pad.finger * 0.85f, ghost); });
        }
    at(phase, [&](ImVec2 p) { dl->AddCircleFilled(p, pad.finger, ToU32(colour)); });
}

} // namespace

void DrawGestureArt(const Ui& ui, ImDrawList* dl, ImVec2 min, ImVec2 size, GestureArt art, double time, const char* key, bool disabled) {
    const ThemeColors& c = ui.C();
    const Color finger = disabled ? c.textDisabled : c.accent;
    const float line = std::max(1.0f, ui.S(1.0f));

    // The key cap on the left, the trackpad on the right.
    Pad pad;
    pad.h = size.y;
    pad.w = std::min(size.x, size.y * 1.35f);
    pad.max = Plus(min, size);
    pad.min = {pad.max.x - pad.w, min.y};
    if (key && *key) {
        ImFont* font = ui.fonts.caption;
        const float fontSize = ui.S(11);
        const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0, key);
        const float capW = std::max(ts.x + ui.S(10), ui.S(26)), capH = ui.S(20);
        const ImVec2 cap{pad.min.x - capW - ui.S(6), min.y + (size.y - capH) / 2};
        const ImVec2 capMax{cap.x + capW, cap.y + capH};
        dl->AddRectFilled({cap.x, cap.y + ui.S(2)}, {capMax.x, capMax.y + ui.S(2)}, ToU32(c.fieldBorder), ui.S(4));
        dl->AddRectFilled(cap, capMax, ToU32(c.panel), ui.S(4));
        dl->AddRect(cap, capMax, ToU32(c.fieldBorder), ui.S(4), 0, line);
        dl->AddText(font, fontSize, {cap.x + (capW - ts.x) / 2, cap.y + (capH - ts.y) / 2},
                    ToU32(disabled ? c.textDisabled : c.textSecondary), key);
    }
    pad.centre = {(pad.min.x + pad.max.x) / 2, (pad.min.y + pad.max.y) / 2};
    pad.finger = pad.h * 0.085f;
    dl->AddRectFilled(pad.min, pad.max, ToU32(c.field), pad.h * 0.14f);
    dl->AddRect(pad.min, pad.max, ToU32(c.fieldBorder), pad.h * 0.14f, 0, line);

    const bool moving = !disabled;
    const auto still = [&](double period) { return moving ? Phase(time, period) : 0.3f; };
    switch (art) {
        case GestureArt::TwoFingers: {
            Fingers(dl, pad, still(1.8), finger, moving, [&](float p, auto draw) {
                const float y = (BackAndForth(p) - 0.5f) * 0.42f * pad.h;
                draw(Plus(pad.centre, {-0.12f * pad.w, y}));
                draw(Plus(pad.centre, {0.12f * pad.w, y}));
            });
            break;
        }
        case GestureArt::Pinch: {
            const ImVec2 dir{0.7071f, -0.7071f};
            Fingers(dl, pad, still(1.8), finger, moving, [&](float p, auto draw) {
                const float d = (0.09f + 0.22f * BackAndForth(p)) * pad.w;
                draw(Plus(pad.centre, Times(dir, d)));
                draw(Plus(pad.centre, Times(dir, -d)));
            });
            break;
        }
        case GestureArt::Rotate: {
            const float r = 0.2f * pad.w;
            dl->AddCircle(pad.centre, r, ToU32(c.fieldBorder), 32, line);
            Fingers(dl, pad, still(2.0), finger, moving, [&](float p, auto draw) {
                const float a = kPi / 4 - 0.7f + 1.4f * BackAndForth(p);
                const ImVec2 v{std::cos(a) * r, -std::sin(a) * r};
                draw(Plus(pad.centre, v));
                draw(Plus(pad.centre, Times(v, -1.0f)));
            });
            break;
        }
        case GestureArt::DoubleTap: {
            const float p = still(1.6);
            // Two taps: at 5–15 % and 25–35 % of the loop.
            const auto tap = [&](float start) { return p >= start && p < start + 0.1f ? (p - start) / 0.1f : -1.0f; };
            const float t = std::max(tap(0.05f), tap(0.25f));
            for (const float sx : {-0.12f, 0.12f}) {
                const ImVec2 at = Plus(pad.centre, {sx * pad.w, 0});
                if (t >= 0) dl->AddCircle(at, pad.finger * (1.2f + 1.3f * t), ToU32(finger.WithAlpha(0.6f * (1 - t))), 24, line * 1.5f);
                dl->AddCircleFilled(at, pad.finger, ToU32(finger.WithAlpha(t >= 0 || disabled ? 1.0f : 0.45f)));
            }
            break;
        }
        case GestureArt::ForceClick: {
            const float p = still(1.8);
            const float press = std::clamp((p - 0.1f) / 0.4f, 0.0f, 1.0f);   // pressing harder
            const bool click = p >= 0.5f && p < 0.62f;                       // and the deep click
            if (press > 0 && p < 0.75f)
                dl->AddCircle(pad.centre, pad.finger * (1.3f + 0.9f * press), ToU32(finger.WithAlpha(0.25f + 0.35f * press)), 32,
                              line * (click ? 2.5f : 1.2f));
            if (click) dl->AddCircle(pad.centre, pad.finger * 2.9f, ToU32(finger.WithAlpha(0.35f)), 32, line);
            dl->AddCircleFilled(pad.centre, pad.finger * (click ? 0.85f : 1.0f), ToU32(click && !disabled ? c.accentHover : finger));
            break;
        }
    }
}

} // namespace bz::ui
