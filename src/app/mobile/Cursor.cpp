#include "app/mobile/Cursor.h"

#include "app/mobile/TouchInput.h"
#include "app/sketch/SketchEditor.h"
#include "core/I18n.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace bz::app::mobile {
void DrawDropCursor(AppContext& app, ImDrawList* dl) {
    // As in the approved mockup (canvas "BreezeCAD Mobile — макет", board 2):
    // an outlined drop, its point the tip and its round end under the finger
    // (a ring marks the finger's spot), the tip a dot in the sketch's
    // highlight colour with a white rim.
    const MobileState& m = app.shell.mobile;
    if (!CursorActive(app) || !m.cursorShown) return;
    const ui::ThemeColors& c = app.ui.C();
    // The labels' scale on top of the interface scale (at 125 % the drop stayed small).
    const float k = static_cast<float>(app.settings.mobile.labelScale) * app.ui.scale;
    const ImVec2 tip = m.cursorTip;
    // From the tip toward the round end: down, unless the tip is pressed
    // against the view's edge and the finger is off to a side.
    ImVec2 d{m.cursorFinger.x - tip.x, m.cursorFinger.y - tip.y};
    const float len = std::hypot(d.x, d.y);
    d = len > 1.0f ? ImVec2{d.x / len, d.y / len} : ImVec2{0, 1};
    const ImVec2 r{d.y, -d.x};   // across, to the right of "down"
    const float reach = CursorReach(app), radius = 21.0f * k;
    const auto at = [&](float across, float along) { return ImVec2{tip.x + r.x * across + d.x * along, tip.y + r.y * across + d.y * along}; };
    const ImVec2 centre = at(0, reach);

    // «Линза» (the author's choice, mockup F10, 18.10.2026): the drop's point stops short of
    // the tip and goes on as a needle — the point worked on is never covered; the round end is
    // a see-through lens over the finger with a soft highlight; a shadow keeps it off any
    // background. Colours: the accent at work, green on a snap (with the target's shape at the
    // tip), orange in the precise mode; ✓ held fills it.
    const MobileSettings& ms = app.settings.mobile;
    Session* s = app.Doc();
    const SnapKind snap = s && s->mode == EditMode::Sketch ? app.sketch.CurrentSnap() : SnapKind::None;
    const bool snapped = snap == SnapKind::Point || snap == SnapKind::Midpoint || snap == SnapKind::Curve ||
                         snap == SnapKind::Intersection;
    const bool precise = ms.cursorSpeed < 0.999;
    const Color ink = snapped ? c.success : precise ? c.warning : c.accent;
    const float gap = 9.0f * k;   // the drop's point ends this far from the tip
    const ImVec2 point = at(0, gap);
    const float bodyLen = reach - gap;
    const auto outline = [&](ImVec2 shift) {
        const auto q = [&](float across, float along) {
            const ImVec2 v = at(across, gap + along);
            return ImVec2{v.x + shift.x, v.y + shift.y};
        };
        dl->PathLineTo({point.x + shift.x, point.y + shift.y});
        dl->PathBezierCubicCurveTo(q(9 * k, bodyLen * 0.35f), q(radius * 0.95f, bodyLen * 0.62f), q(radius, bodyLen));
        const float a0 = std::atan2(r.y, r.x);
        dl->PathArcTo({centre.x + shift.x, centre.y + shift.y}, radius, a0, a0 + 3.14159265f, 28);
        dl->PathBezierCubicCurveTo(q(-radius * 0.95f, bodyLen * 0.62f), q(-9 * k, bodyLen * 0.35f), {point.x + shift.x, point.y + shift.y});
        if (dl->_Path.Size > 1) dl->_Path.pop_back();   // no seam from the repeated first point (a concave fill drew one)
    };
    // The shadow, a little down.
    outline({0, 2.0f * k});
    dl->PathStroke(ui::ToU32(Color{0, 0, 0, 0.35f}), ImDrawFlags_Closed, 4.0f * k);
    // The body: see-through, fuller while ✓ is held.
    outline({0, 0});
    dl->PathFillConvex(ui::ToU32(ink.WithAlpha(m.okDown ? 0.38f : m.cursorDragging ? 0.18f : 0.13f)));
    // The lens's highlight, top left of the round end.
    dl->AddCircleFilled(at(-radius * 0.3f, reach - radius * 0.35f), radius * 0.45f, ui::ToU32(Color{1, 1, 1, 0.10f}), 24);
    dl->AddCircleFilled(at(-radius * 0.38f, reach - radius * 0.48f), radius * 0.18f, ui::ToU32(Color{1, 1, 1, 0.16f}), 16);
    outline({0, 0});
    dl->PathStroke(ui::ToU32(ink), ImDrawFlags_Closed, 1.8f * k);

    // «Зажать»: an amber ring round the tip — "I hold it" — and a soft disc inside.
    // The palm on and ✓ not pressed yet: the ring thin and faint — "ready to hold".
    if (m.holding) {
        const Color hold = c.warning;
        dl->AddCircleFilled(tip, 13.0f * k, ui::ToU32(hold.WithAlpha(0.16f)), 32);
        dl->AddCircle(tip, 13.0f * k, ui::ToU32(Color{0, 0, 0, 0.35f}), 32, 3.6f * k);
        dl->AddCircle(tip, 13.0f * k, ui::ToU32(hold), 32, 2.2f * k);
    } else if (m.holdArmed) {
        dl->AddCircle(tip, 13.0f * k, ui::ToU32(c.warning.WithAlpha(0.55f)), 32, 1.2f * k);
    }
    // The needle on to the tip, a halo round it, the tip itself.
    const ImVec2 needleEnd = at(0, 4.0f * k);
    dl->AddLine(point, needleEnd, ui::ToU32(ink), 1.4f * k);
    dl->AddCircle(tip, 7.0f * k, ui::ToU32(ink.WithAlpha(0.55f)), 24, 1.0f * k);
    dl->AddCircleFilled(tip, 3.4f * k, ui::ToU32(Color{0, 0, 0, 0.45f}), 12);
    dl->AddCircleFilled(tip, 2.4f * k, ui::ToU32(Color{1, 1, 1, 1}), 12);
    // What the tip snapped to: a square (a point), a triangle (a middle), ✕ (on a curve).
    const float sq = 6.0f * k;
    if (snap == SnapKind::Point) {
        dl->AddRect({tip.x - sq, tip.y - sq}, {tip.x + sq, tip.y + sq}, ui::ToU32(ink), 1.5f * k, 0, 1.6f * k);
    } else if (snap == SnapKind::Midpoint) {
        dl->AddTriangle({tip.x, tip.y - sq * 1.2f}, {tip.x + sq * 1.1f, tip.y + sq * 0.8f}, {tip.x - sq * 1.1f, tip.y + sq * 0.8f}, ui::ToU32(ink), 1.6f * k);
    } else if (snap == SnapKind::Curve || snap == SnapKind::Intersection) {
        dl->AddLine({tip.x - sq, tip.y - sq}, {tip.x + sq, tip.y + sq}, ui::ToU32(ink), 1.6f * k);
        dl->AddLine({tip.x - sq, tip.y + sq}, {tip.x + sq, tip.y - sq}, ui::ToU32(ink), 1.6f * k);
    }

    // The mockup's hint beside the drop while it rests (a newcomer's question
    // is what to drag): to the side with room, never while dragging.
    if (!m.cursorDragging && !m.holding) {
        const char* hint = Tr("тяни круг — кончик ведёт");
        const float fs = 11.0f * k;
        const ImVec2 ts = app.ui.fonts.bodySmall->CalcTextSizeA(fs, FLT_MAX, 0, hint);
        const bool toLeft = centre.x + radius + 8 + ts.x + 12 > m.viewMax.x;
        const float x = toLeft ? centre.x - radius - 8 - ts.x - 12 : centre.x + radius + 8;
        const ImVec2 a{x, centre.y - ts.y / 2 - 4}, b{x + ts.x + 12, centre.y + ts.y / 2 + 4};
        dl->AddRectFilled(a, b, ui::ToU32(c.panel.WithAlpha(0.9f)), 6.0f);
        dl->AddText(app.ui.fonts.bodySmall, fs, {a.x + 6, a.y + 4}, ui::ToU32(c.textSecondary), hint);
    }
}

void DrawAim(AppContext& app, ImDrawList* dl) {
    const MobileState& m = app.shell.mobile;
    if (!m.aiming) return;
    const ui::ThemeColors& c = app.ui.C();
    const float k = static_cast<float>(app.settings.mobile.labelScale) * app.ui.scale;
    const ImVec2 f = m.aimFinger, t = m.aimTip;
    // Where the finger is: a soft ring; from it up to the aim, a thin line.
    dl->AddCircleFilled(f, 22.0f * k, ui::ToU32(c.accent.WithAlpha(0.12f)), 32);
    dl->AddCircle(f, 22.0f * k, ui::ToU32(c.accent.WithAlpha(0.45f)), 32, 1.5f * k);
    dl->AddLine({f.x, f.y - 22.0f * k}, {t.x, t.y + 6.0f * k}, ui::ToU32(c.accent.WithAlpha(0.45f)), 1.0f * k);
    // The aim: a cross with a gap in the middle, so the point under it stays seen.
    const float arm = 16.0f * k, gap = 4.0f * k, w = 2.0f * k;
    const ImU32 ink = ui::ToU32(c.accent);
    const ImU32 halo = ui::ToU32(c.panel.WithAlpha(0.85f));
    const auto cross = [&](ImU32 col, float width) {
        dl->AddLine({t.x - arm, t.y}, {t.x - gap, t.y}, col, width);
        dl->AddLine({t.x + gap, t.y}, {t.x + arm, t.y}, col, width);
        dl->AddLine({t.x, t.y - arm}, {t.x, t.y - gap}, col, width);
        dl->AddLine({t.x, t.y + gap}, {t.x, t.y + arm}, col, width);
    };
    cross(halo, w + 2.0f * k);
    cross(ink, w);
    dl->AddCircleFilled(t, 1.5f * k, ink);
}

void DrawPressRing(AppContext& app, ImDrawList* dl) {
    const MobileState& m = app.shell.mobile;
    if (m.pressProgress <= 0.0f) return;
    const ui::ThemeColors& c = app.ui.C();
    const float k = static_cast<float>(app.settings.mobile.labelScale) * app.ui.scale;
    // Above the finger's spot, so the finger does not hide it.
    const ImVec2 at{m.pressAt.x, m.pressAt.y};
    const float  radius = 30.0f * k, width = 5.0f * k;
    const float  t = std::clamp(m.pressProgress, 0.0f, 1.0f);
    dl->AddCircle(at, radius, ui::ToU32(c.textSecondary.WithAlpha(0.35f)), 48, width);
    constexpr float kTop = -1.5707963f, kTurn = 6.2831853f;
    dl->PathArcTo(at, radius, kTop, kTop + kTurn * t, 48);
    dl->PathStroke(ui::ToU32(c.accent), 0, width);
    dl->AddCircleFilled(at, 3.0f * k, ui::ToU32(c.accent));
}

} // namespace bz::app::mobile
