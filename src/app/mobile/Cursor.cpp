#include "app/mobile/Cursor.h"

#include "app/mobile/TouchInput.h"
#include "ui/Gl.h"

#include <algorithm>
#include <cmath>

namespace bz::app::mobile {
namespace {

constexpr float kRegion = 56.0f;    ///< dp around the tip that the magnifier shows
constexpr float kZoom = 2.2f;       ///< how much bigger

ImVec2 MagnifierCentre(const MobileState& m, float radius) {
    // Above the tip, or beside it toward the middle when there is no room above.
    ImVec2 c{m.cursorTip.x, m.cursorTip.y - radius - 34.0f};
    if (c.y - radius < m.viewMin.y) {
        const bool left = m.cursorTip.x > (m.viewMin.x + m.viewMax.x) / 2;
        c = {m.cursorTip.x + (left ? -1.0f : 1.0f) * (radius + 46.0f), std::max(m.cursorTip.y, m.viewMin.y + radius + 4.0f)};
    }
    return c;
}

} // namespace

void DrawDropCursor(AppContext& app, ImDrawList* dl) {
    const MobileState& m = app.shell.mobile;
    if (!CursorActive(app) || !m.cursorShown) return;
    const ui::ThemeColors& c = app.ui.C();
    const float k = static_cast<float>(app.settings.mobile.labelScale);
    const ImVec2 tip = m.cursorTip;
    // The round part sits where the finger is (reach below the tip, or the
    // tip pressed against the view's edge).
    const ImVec2 body = m.cursorFinger;
    const float r = 20.0f * k;
    const float alpha = m.cursorDragging ? 0.5f : 0.32f;

    // The drop: the circle and a tail narrowing to the tip.
    const ImVec2 d{tip.x - body.x, tip.y - body.y};
    const float len = std::max(1.0f, std::hypot(d.x, d.y));
    const ImVec2 n{-d.y / len, d.x / len};   // across the tail
    if (len > r) {
        const ImVec2 a{body.x + n.x * r * 0.62f, body.y + n.y * r * 0.62f};
        const ImVec2 b{body.x - n.x * r * 0.62f, body.y - n.y * r * 0.62f};
        dl->AddTriangleFilled(a, b, tip, ui::ToU32(c.accent.WithAlpha(alpha)));
    }
    dl->AddCircleFilled(body, r, ui::ToU32(c.accent.WithAlpha(alpha)), 40);
    dl->AddCircle(body, r, ui::ToU32(c.accent.WithAlpha(0.85f)), 40, 1.5f * k);

    // The tip: a ring with a dot, white under it so it reads on any colour.
    const float t = 6.0f * k;
    dl->AddCircle(tip, t + 1.5f * k, ui::ToU32(Color{1, 1, 1, 0.9f}), 24, 3.0f * k);
    dl->AddCircle(tip, t, ui::ToU32(c.danger), 24, 2.0f * k);
    dl->AddCircleFilled(tip, 1.8f * k, ui::ToU32(c.danger));
    const float h = t + 5.0f * k;
    dl->AddLine({tip.x - h, tip.y}, {tip.x - t - 1, tip.y}, ui::ToU32(c.danger), 1.4f * k);
    dl->AddLine({tip.x + t + 1, tip.y}, {tip.x + h, tip.y}, ui::ToU32(c.danger), 1.4f * k);
    dl->AddLine({tip.x, tip.y - h}, {tip.x, tip.y - t - 1}, ui::ToU32(c.danger), 1.4f * k);
    dl->AddLine({tip.x, tip.y + t + 1}, {tip.x, tip.y + h}, ui::ToU32(c.danger), 1.4f * k);
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
    const float radius = kRegion * kZoom / 2;
    const ImVec2 centre = MagnifierCentre(m, radius);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ui::ThemeColors& c = app.ui.C();
    dl->AddCircleFilled({centre.x, centre.y + 2}, radius + 3, ui::ToU32(Color{0, 0, 0, 0.18f}), 48);
    // The texture holds the window's rows bottom-up (OpenGL): flipped here.
    dl->AddImageRounded(static_cast<ImTextureID>(myTexture), {centre.x - radius, centre.y - radius}, {centre.x + radius, centre.y + radius},
                        {0, 1}, {1, 0}, IM_COL32_WHITE, radius);
    dl->AddCircle(centre, radius, ui::ToU32(c.accent), 48, 2.0f);
    // The tip's place in the middle.
    dl->AddCircle(centre, 3.0f, ui::ToU32(c.danger), 16, 1.5f);
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
