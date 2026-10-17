#include "app/mobile/Cursor.h"

#include "app/mobile/TouchInput.h"
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
    const float k = static_cast<float>(app.settings.mobile.labelScale);
    const ImVec2 tip = m.cursorTip;
    // From the tip toward the round end: down, unless the tip is pressed
    // against the view's edge and the finger is off to a side.
    ImVec2 d{m.cursorFinger.x - tip.x, m.cursorFinger.y - tip.y};
    const float len = std::hypot(d.x, d.y);
    d = len > 1.0f ? ImVec2{d.x / len, d.y / len} : ImVec2{0, 1};
    const ImVec2 r{d.y, -d.x};   // across, to the right of "down"
    const float reach = CursorReach(app), radius = 24.0f * k;
    const auto at = [&](float across, float along) { return ImVec2{tip.x + r.x * across + d.x * along, tip.y + r.y * across + d.y * along}; };
    const ImVec2 centre = at(0, reach);

    const auto outline = [&] {
        dl->PathLineTo(tip);
        dl->PathBezierCubicCurveTo(at(12 * k, 18 * k), at(24 * k, 32 * k), at(radius, reach));
        const float a0 = std::atan2(r.y, r.x);
        dl->PathArcTo(centre, radius, a0, a0 + 3.14159265f, 28);
        dl->PathBezierCubicCurveTo(at(-24 * k, 32 * k), at(-12 * k, 18 * k), tip);
        // The path came back to the tip: that last point repeats the first. The
        // concave fill drew a light seam from the tip into the drop along it
        // ("a stripe inside the cursor"); the drop is convex, filled as such.
        if (dl->_Path.Size > 1) dl->_Path.pop_back();
    };
    outline();
    dl->PathFillConvex(ui::ToU32(Color{1, 1, 1, m.cursorDragging ? 0.55f : 0.4f}));
    outline();
    dl->PathStroke(ui::ToU32(c.accent), ImDrawFlags_Closed, 2.0f * k);

    // The finger's spot: a plain ring (it was dashed — looked broken).
    const float ring = 12.0f * k;
    dl->AddCircleFilled(centre, ring, ui::ToU32(Color{1, 1, 1, 0.75f}), 32);
    dl->AddCircle(centre, ring, ui::ToU32(c.accent), 32, 1.5f * k);

    // The tip.
    dl->AddCircleFilled(tip, 4.5f * k + 2.0f * k, ui::ToU32(Color{1, 1, 1, 1}), 16);
    dl->AddCircleFilled(tip, 4.5f * k, ui::ToU32(c.sketchSelected), 16);

    // The mockup's hint beside the drop while it rests (a newcomer's question
    // is what to drag): to the side with room, never while dragging.
    if (!m.cursorDragging) {
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

} // namespace bz::app::mobile
