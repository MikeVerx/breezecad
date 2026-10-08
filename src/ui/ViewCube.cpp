#include "ui/ViewCube.h"

#include "ui/Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace bz::ui {
namespace {

struct CubeFace {
    Vec3        n, t1, t2;   ///< outward normal; text right and text down on the face
    const char* text;
};

// Z-up model, the front face looks toward -Y.
constexpr std::array<CubeFace, 6> kFaces{{
    {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}, "СВЕРХУ"},
    {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}, "СНИЗУ"},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}, "СПЕРЕДИ"},
    {{0, 1, 0}, {-1, 0, 0}, {0, 0, -1}, "СЗАДИ"},
    {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}, "СПРАВА"},
    {{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}, "СЛЕВА"},
}};

constexpr double kBevel = 0.7;   // faces are split at +-0.7 into 9 regions

Color Mix(const Color& a, const Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

/// Rotates `a` around unit axis `k` by `angle` (Rodrigues).
Vec3 Rotate(Vec3 a, Vec3 k, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return a * c + k.Cross(a) * s + k * (k.Dot(a) * (1 - c));
}

/// Nearest of +-X, +-Y to a horizontal direction.
Vec3 SnapHorizontal(Vec3 d) {
    if (std::abs(d.x) >= std::abs(d.y)) return {d.x >= 0 ? 1.0 : -1.0, 0, 0};
    return {0, d.y >= 0 ? 1.0 : -1.0, 0};
}

bool PointInQuad(ImVec2 p, const std::array<ImVec2, 4>& q) {
    bool inside = false;
    for (std::size_t i = 0, j = 3; i < 4; j = i++) {
        if ((q[i].y > p.y) != (q[j].y > p.y) &&
            p.x < (q[j].x - q[i].x) * (p.y - q[i].y) / (q[j].y - q[i].y) + q[i].x)
            inside = !inside;
    }
    return inside;
}

/// Small floating button with a hover background. Returns true on click.
bool FloatingButton(Ui& ui, const char* id, ImVec2 center, float size, const char* tooltip) {
    const ImVec2 p{center.x - size / 2, center.y - size / 2};
    ImGui::SetCursorScreenPos(p);
    const bool clicked = ImGui::InvisibleButton(id, {size, size});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + size, p.y + size}, ToU32(hovered ? ui.C().hover : ui.C().floatingButton), ui.S(5));
    dl->AddRect(p, {p.x + size, p.y + size}, ToU32(ui.C().floatingBorder), ui.S(5), 0, ui.S(1));
    if (tooltip) ItemTooltip(ui, tooltip);
    return clicked;
}

void Triangle(ImDrawList* dl, ImVec2 c, ImVec2 dir, float size, ImU32 color) {
    const ImVec2 n{-dir.y, dir.x};
    dl->AddTriangleFilled({c.x + dir.x * size, c.y + dir.y * size},
                          {c.x - dir.x * size * 0.6f + n.x * size * 0.8f, c.y - dir.y * size * 0.6f + n.y * size * 0.8f},
                          {c.x - dir.x * size * 0.6f - n.x * size * 0.8f, c.y - dir.y * size * 0.6f - n.y * size * 0.8f},
                          color);
}

} // namespace

ViewCubeAction DrawViewCube(Ui& ui, const ViewProjection& view, ImVec2 topLeft, ImVec2 bottomRight) {
    ViewCubeAction action;
    const ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Camera basis: r to the right of the screen, u up, v toward the viewer.
    const Vec3 d = view.direction.Normalized();
    const Vec3 r = d.Cross(view.up).Normalized();
    const Vec3 u = r.Cross(d).Normalized();
    const Vec3 v = -d;
    const Vec3 light = (v * 0.8 + u * 0.55 - r * 0.35).Normalized();

    const float cs = ui.S(33);
    const ImVec2 center{bottomRight.x - ui.S(86), topLeft.y + ui.S(86)};
    const auto project = [&](Vec3 q) {
        return ImVec2{center.x + cs * static_cast<float>(q.Dot(r)), center.y - cs * static_cast<float>(q.Dot(u))};
    };

    // Interaction area: dragging it orbits, a click picks a region.
    const float area = cs * 2.0f;
    ImGui::SetCursorScreenPos({center.x - area, center.y - area});
    ImGui::InvisibleButton("##viewcube", {area * 2, area * 2});
    const bool cubeHovered = ImGui::IsItemHovered();
    const bool cubeActive = ImGui::IsItemActive();
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool dragging = cubeActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f);
    if (dragging) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        action.dragYaw = static_cast<double>(delta.x) * 0.6 * kPi / 180.0;
        action.dragPitch = static_cast<double>(delta.y) * 0.6 * kPi / 180.0;
    }

    // Regions: 9 per visible face (face centre, 4 edges, 4 corners).
    struct Region {
        std::array<ImVec2, 4> quad;
        Vec3                  dir;
        Color                 fill;
    };
    // At most three faces are visible: 27 regions, no allocation per frame.
    std::array<Region, 27> regions{};
    std::size_t regionCount = 0;
    std::optional<std::size_t> hoveredRegion;
    const double los[3] = {-1, -kBevel, kBevel}, his[3] = {-kBevel, kBevel, 1};
    for (const CubeFace& f : kFaces) {
        if (f.n.Dot(v) <= 0.02) continue;
        const float lam = static_cast<float>(std::max(0.0, f.n.Dot(light)));
        for (int i = -1; i <= 1; ++i)
            for (int j = -1; j <= 1; ++j) {
                const auto corner = [&](double pu, double pw) { return project(f.n + f.t1 * pu + f.t2 * pw); };
                const std::size_t ii = static_cast<std::size_t>(i + 1), jj = static_cast<std::size_t>(j + 1);
                Region reg;
                reg.quad = {corner(los[ii], los[jj]), corner(his[ii], los[jj]), corner(his[ii], his[jj]), corner(los[ii], his[jj])};
                reg.dir = f.n + f.t1 * i + f.t2 * j;
                const int bevel = (i != 0) + (j != 0);
                reg.fill = Mix(c.cubeShade, c.cubeLight, 0.32f + 0.68f * lam - static_cast<float>(bevel) * 0.07f);
                if (regionCount == regions.size()) continue;
                regions[regionCount] = reg;
                if (cubeHovered && !dragging && PointInQuad(mouse, reg.quad)) hoveredRegion = regionCount;
                ++regionCount;
            }
    }

    // Soft shadow, faces, outlines, labels.
    dl->AddCircleFilled({center.x + ui.S(2), center.y + ui.S(4)}, cs * 1.55f, ToU32(Color{0, 0, 0, ui.theme.dark ? 0.18f : 0.05f}), 32);
    for (std::size_t k = 0; k < regionCount; ++k) {
        const Region& reg = regions[k];
        const Color fill = hoveredRegion == k ? c.cubeHover : reg.fill;
        dl->AddConvexPolyFilled(reg.quad.data(), 4, ToU32(fill));
    }
    for (const CubeFace& f : kFaces) {
        if (f.n.Dot(v) <= 0.02) continue;
        const auto pt = [&](double a, double b) { return project(f.n + f.t1 * a + f.t2 * b); };
        const ImVec2 inner[4] = {pt(-kBevel, -kBevel), pt(kBevel, -kBevel), pt(kBevel, kBevel), pt(-kBevel, kBevel)};
        dl->AddPolyline(inner, 4, ToU32(c.cubeInner), ImDrawFlags_Closed, ui.S(1));
        const ImVec2 outer[4] = {pt(-1, -1), pt(1, -1), pt(1, 1), pt(-1, 1)};
        dl->AddPolyline(outer, 4, ToU32(c.cubeStroke), ImDrawFlags_Closed, ui.S(1.2f));

        // Face label: text drawn flat, then its vertices mapped onto the face.
        if (f.n.Dot(v) < 0.18) continue;
        ImFont* font = ui.fonts.strong;
        const float fs = ui.S(11);
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, f.text);
        const float fit = std::min(1.0f, 1.25f * cs / ts.x);
        const ImVec2 o = project(f.n);
        const ImVec2 a1 = project(f.n + f.t1), a2 = project(f.n + f.t2);
        const ImVec2 e1{a1.x - o.x, a1.y - o.y}, e2{a2.x - o.x, a2.y - o.y};
        const int start = dl->VtxBuffer.Size;
        const ImVec2 at{o.x - ts.x / 2, o.y - ts.y / 2};
        dl->AddText(font, fs, at, ToU32(c.cubeText), f.text);
        for (int vi = start; vi < dl->VtxBuffer.Size; ++vi) {
            ImDrawVert& vert = dl->VtxBuffer[vi];
            const float lx = (vert.pos.x - o.x) * fit / cs;
            const float ly = (vert.pos.y - o.y) * fit / cs;
            vert.pos = {o.x + e1.x * lx + e2.x * ly, o.y + e1.y * lx + e2.y * ly};
        }
    }

    // Click on a region: look from that face, edge or corner.
    if (ImGui::IsItemDeactivated() && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left, 3.0f)) {
        for (std::size_t k = 0; k < regionCount; ++k) {
            const Region& reg = regions[k];
            if (!PointInQuad(mouse, reg.quad)) continue;
            const Vec3 dir = reg.dir.Normalized();
            Vec3 up{0, 0, 1};
            if (std::abs(dir.z) > 0.999) {
                // Top or bottom: keep the current heading so the view does not spin.
                Vec3 heading{d.x, d.y, 0};
                if (heading.Length() < 1e-3) heading = {u.x, u.y, 0};
                up = SnapHorizontal(heading);
            }
            action.lookFrom = std::make_pair(dir, up);
            break;
        }
    }

    // When looking straight at a face: arrows to the neighbouring faces and roll.
    const bool aligned = std::abs(std::abs(d.x) - 1) < 1e-3 || std::abs(std::abs(d.y) - 1) < 1e-3 ||
                         std::abs(std::abs(d.z) - 1) < 1e-3;
    const float reach = cs * 1.95f;
    const float btn = ui.S(20);
    const ImU32 ink = ToU32(c.accent);
    if (aligned) {
        struct Arrow {
            const char* id;
            ImVec2      dir;
            Vec3        axis;
            double      angle;
        };
        const Arrow arrows[4] = {
            {"##cube-up", {0, -1}, r, kPi / 2},
            {"##cube-down", {0, 1}, r, -kPi / 2},
            {"##cube-left", {-1, 0}, u, -kPi / 2},
            {"##cube-right", {1, 0}, u, kPi / 2},
        };
        for (const Arrow& a : arrows) {
            const ImVec2 p{center.x + a.dir.x * reach, center.y + a.dir.y * reach};
            ImGui::SetCursorScreenPos({p.x - btn / 2, p.y - btn / 2});
            if (ImGui::InvisibleButton(a.id, {btn, btn}))
                action.lookFrom = std::make_pair(Rotate(v, a.axis, a.angle).Normalized(), Rotate(u, a.axis, a.angle).Normalized());
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) dl->AddRectFilled({p.x - btn / 2, p.y - btn / 2}, {p.x + btn / 2, p.y + btn / 2}, ToU32(c.hover), ui.S(5));
            Triangle(dl, p, a.dir, ui.S(5), ink);
        }
        // Roll the view by 90 degrees around the line of sight.
        const ImVec2 rollPos[2] = {{center.x + reach * 0.78f, center.y - reach * 0.78f},
                                   {center.x + reach * 0.98f, center.y - reach * 0.52f}};
        const char* rollIds[2] = {"##roll-ccw", "##roll-cw"};
        for (int k = 0; k < 2; ++k) {
            const ImVec2 p = rollPos[k];
            ImGui::SetCursorScreenPos({p.x - btn / 2, p.y - btn / 2});
            const double angle = k == 0 ? kPi / 2 : -kPi / 2;
            if (ImGui::InvisibleButton(rollIds[k], {btn, btn})) action.lookFrom = std::make_pair(v, Rotate(u, v, angle).Normalized());
            if (ImGui::IsItemHovered()) dl->AddRectFilled({p.x - btn / 2, p.y - btn / 2}, {p.x + btn / 2, p.y + btn / 2}, ToU32(c.hover), ui.S(5));
            const float rr = ui.S(6);
            const float a0 = k == 0 ? -0.2f : 3.34f, a1 = k == 0 ? 3.6f : -0.46f;
            dl->PathArcTo(p, rr, a0, a1, 12);
            dl->PathStroke(ink, 0, ui.S(1.6f));
            const ImVec2 tip{p.x + rr * std::cos(a1), p.y + rr * std::sin(a1)};
            Triangle(dl, tip, {k == 0 ? 0.3f : -0.3f, 1.0f}, ui.S(3.2f), ink);
        }
    }

    // Home button.
    const ImVec2 homeCenter{center.x - reach * 0.86f, center.y - reach * 0.86f};
    if (FloatingButton(ui, "##cube-home", homeCenter, ui.S(26), "Домой: изометрия (Home)")) action.home = true;
    ui.icons.Draw(dl, "ui-home", {homeCenter.x - ui.S(8), homeCenter.y - ui.S(8)}, ui.S(16), c.floatingInk);

    // Axis triad in the bottom-left corner, far axes first.
    const ImVec2 origin{topLeft.x + ui.S(44), bottomRight.y - ui.S(44)};
    struct Axis {
        Vec3        e;
        Color       color;
        const char* label;
    };
    std::array<Axis, 3> axes{{{{1, 0, 0}, c.axisX, "X"}, {{0, 1, 0}, c.axisY, "Y"}, {{0, 0, 1}, c.axisZ, "Z"}}};
    std::sort(axes.begin(), axes.end(), [&](const Axis& a, const Axis& b) { return a.e.Dot(v) < b.e.Dot(v); });
    const float len = ui.S(24);
    for (const Axis& a : axes) {
        const ImVec2 tip{origin.x + len * static_cast<float>(a.e.Dot(r)), origin.y - len * static_cast<float>(a.e.Dot(u))};
        dl->AddLine(origin, tip, ToU32(a.color), ui.S(2.2f));
        dl->AddCircleFilled(tip, ui.S(2.2f), ToU32(a.color));
        const ImVec2 lp{origin.x + (tip.x - origin.x) * 1.42f - ui.S(3.5f), origin.y + (tip.y - origin.y) * 1.42f - ui.S(6)};
        dl->AddText(ui.fonts.strong, ui.S(11), lp, ToU32(a.color), a.label);
    }
    dl->AddCircleFilled(origin, ui.S(2.5f), ToU32(c.textSecondary));
    return action;
}

} // namespace bz::ui
