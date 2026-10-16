#include "app/mobile/Cursor.h"

#include "app/mobile/TouchInput.h"
#include "core/I18n.h"
#include "ui/Gl.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace bz::app::mobile {
namespace {

constexpr float kRadius = 38.0f;    ///< the magnifier's circle (dp), as in the mockup
constexpr float kZoom = 4.0f;       ///< "лупа × 4"
constexpr float kRegion = 2 * kRadius / kZoom;   ///< dp around the tip it shows

ImVec2 MagnifierCentre(const MobileState& m, float radius) {
    // Up and to the right of the tip (the mockup), mirrored toward the middle
    // at the view's edges.
    const bool left = m.cursorTip.x + 50.0f + radius > m.viewMax.x;
    ImVec2 c{m.cursorTip.x + (left ? -50.0f : 50.0f), m.cursorTip.y - 68.0f};
    if (c.y - radius < m.viewMin.y + 4.0f) c.y = m.cursorTip.y + 68.0f + kCursorReach;   // no room above: below the drop
    return c;
}

} // namespace

void DrawDropCursor(AppContext& app, ImDrawList* dl) {
    // As in the approved mockup (canvas "BreezeCAD Mobile — макет", board 2):
    // an outlined drop, its point the tip and its round end under the finger
    // (a dashed ring marks the finger's spot), the tip a dot in the sketch's
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
    const float reach = kCursorReach * k, radius = 24.0f * k;
    const auto at = [&](float across, float along) { return ImVec2{tip.x + r.x * across + d.x * along, tip.y + r.y * across + d.y * along}; };
    const ImVec2 centre = at(0, reach);

    const auto outline = [&] {
        dl->PathLineTo(tip);
        dl->PathBezierCubicCurveTo(at(12 * k, 18 * k), at(24 * k, 32 * k), at(radius, reach));
        const float a0 = std::atan2(r.y, r.x);
        dl->PathArcTo(centre, radius, a0, a0 + 3.14159265f, 28);
        dl->PathBezierCubicCurveTo(at(-24 * k, 32 * k), at(-12 * k, 18 * k), tip);
    };
    outline();
    dl->PathFillConcave(ui::ToU32(Color{1, 1, 1, m.cursorDragging ? 0.55f : 0.4f}));
    outline();
    dl->PathStroke(ui::ToU32(c.accent), ImDrawFlags_Closed, 2.0f * k);

    // The finger's spot: a dashed ring.
    const float ring = 12.0f * k;
    dl->AddCircleFilled(centre, ring, ui::ToU32(Color{1, 1, 1, 0.75f}), 24);
    for (int i = 0; i < 12; i += 2) {
        const float a = static_cast<float>(i) * 3.14159265f / 6.0f;
        dl->PathArcTo(centre, ring, a, a + 3.14159265f / 6.0f, 4);
        dl->PathStroke(ui::ToU32(c.accent), ImDrawFlags_None, 1.2f * k);
    }

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

Magnifier::~Magnifier() {
    if (myTexture) glDeleteTextures(1, &myTexture);
}

bool Magnifier::Wanted(const AppContext& app) const {
    const MobileState& m = app.shell.mobile;
    const Session* s = app.workspace.Active();
    return app.settings.mobile.magnifier && CursorActive(app) && m.cursorShown && m.cursorDragging && s && s->mode == EditMode::Sketch;
}

void Magnifier::Draw(AppContext& app) {
    if (!Wanted(app)) {
        myHas = false;
        return;
    }
    if (!myHas || !myTexture) return;   // the first frame: nothing captured yet
    const MobileState& m = app.shell.mobile;
    const float radius = kRadius;
    const ImVec2 centre = MagnifierCentre(m, radius);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ui::ThemeColors& c = app.ui.C();
    dl->AddCircleFilled(centre, radius, ui::ToU32(Color{1, 1, 1, 1}), 48);
    // The texture holds the window's rows bottom-up (OpenGL): flipped here.
    dl->AddImageRounded(static_cast<ImTextureID>(myTexture), {centre.x - radius, centre.y - radius}, {centre.x + radius, centre.y + radius},
                        {0, 1}, {1, 0}, IM_COL32_WHITE, radius);
    dl->AddCircle(centre, radius, ui::ToU32(c.accent), 48, 1.5f);
    // Where the tip is: a light cross in the middle.
    dl->AddLine({centre.x - 8, centre.y}, {centre.x + 8, centre.y}, ui::ToU32(c.textMuted), 1.0f);
    dl->AddLine({centre.x, centre.y - 8}, {centre.x, centre.y + 8}, ui::ToU32(c.textMuted), 1.0f);
    const char* label = Tr("лупа × 4");
    const ImVec2 ts = app.ui.fonts.body->CalcTextSizeA(10.5f, FLT_MAX, 0, label);
    dl->AddText(app.ui.fonts.body, 10.5f, {centre.x - ts.x / 2, centre.y + radius + 6}, ui::ToU32(c.textSecondary), label);
}

void Magnifier::Capture(const AppContext& app, float density, int windowHeight) {
    if (!Wanted(app)) return;
    const MobileState& m = app.shell.mobile;
    const int size = std::max(8, static_cast<int>(kRegion * density));
    if (!myTexture || mySize != size) {
        if (!myTexture) glGenTextures(1, &myTexture);
        glBindTexture(GL_TEXTURE_2D, myTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // RGB: the window has no alpha, and a copy may not add components (OpenGL ES).
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        mySize = size;
    }
    // The window's pixels around the tip (OpenGL counts rows from the bottom).
    const int x = static_cast<int>(m.cursorTip.x * density) - size / 2;
    const int y = windowHeight - static_cast<int>(m.cursorTip.y * density) - size / 2;
    glBindTexture(GL_TEXTURE_2D, myTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, std::max(0, x), std::max(0, y), size, size);
    glBindTexture(GL_TEXTURE_2D, 0);
    myHas = true;
}

} // namespace bz::app::mobile
