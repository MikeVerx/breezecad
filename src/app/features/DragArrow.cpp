#include "app/features/DragArrow.h"

#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bz::app {

namespace {

/// Where the cursor ray passes closest to the line base + dir * t: that t.
std::optional<double> RayToLine(const ViewportFrame& vf, ImVec2 mouse, Vec3 base, Vec3 dir) {
    Vec3 origin, ray;
    vf.projection.Unproject(vf.ToLocal(mouse), origin, ray);
    const Vec3 w0 = base - origin;
    const double b = dir.Dot(ray), c = ray.Dot(ray), d = dir.Dot(w0), e = ray.Dot(w0);
    const double denom = c - b * b;   // dir is a unit vector
    if (std::abs(denom) < 1e-9) return std::nullopt;   // looking straight along the arrow
    return (b * e - c * d) / denom;
}

/// The cursor ray hits the plane through `centre` across `normal` there.
std::optional<Vec3> RayToPlane(const ViewportFrame& vf, ImVec2 mouse, Vec3 centre, Vec3 normal) {
    Vec3 origin, ray;
    vf.projection.Unproject(vf.ToLocal(mouse), origin, ray);
    const double along = ray.Dot(normal);
    if (std::abs(along) < 1e-6 * std::max(ray.Length(), 1e-9)) return std::nullopt;   // plane seen edge on
    return origin + ray * ((centre - origin).Dot(normal) / along);
}

/// The marker drawn at the end of a handle: a ring, bigger when hot.
void DrawKnob(ui::Ui& ui, ImDrawList* dl, ImVec2 at, ImU32 ink, bool hot) {
    const float radius = ui.S(hot ? 7.5f : 6.0f);
    dl->AddCircleFilled(at, radius, ui::ToU32(ui.C().panel));
    dl->AddCircle(at, radius, ink, 24, ui.S(2.2f));
}

/// The dragged value next to the handle, on a plate so it reads over the body.
void DrawValue(ui::Ui& ui, ImDrawList* dl, ImVec2 at, ImU32 ink, const std::string& label) {
    if (label.empty()) return;
    const ui::ThemeColors& c = ui.C();
    const float size = ui.S(13);
    const ImVec2 extent = ui.fonts.mono->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
    const ImVec2 p{at.x + ui.S(14), at.y - ui.S(22)};
    const float pad = ui.S(4);
    dl->AddRectFilled({p.x - pad, p.y - pad}, {p.x + extent.x + pad, p.y + extent.y + pad}, ui::ToU32(c.panel), ui.S(4));
    dl->AddRect({p.x - pad, p.y - pad}, {p.x + extent.x + pad, p.y + extent.y + pad}, ink, ui.S(4));
    dl->AddText(ui.fonts.mono, size, p, ui::ToU32(c.text), label.c_str());
}

} // namespace

double DragStep(double mmPerPixel) {
    const double target = std::max(mmPerPixel * 4.0, 1e-3);
    const double power = std::pow(10.0, std::floor(std::log10(target)));
    for (double m : {1.0, 2.0, 5.0})
        if (power * m >= target) return power * m;
    return power * 10.0;
}

std::optional<double> DragArrow::Update(AppContext& app, const ViewportFrame& vf, Vec3 base, Vec3 dir, double length,
                                        const std::string& label) {
    const Vec3 tip = base + dir * length;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = vf.Project(base), b = vf.Project(tip);
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool overTip = vf.hovered && std::hypot(mouse.x - b.x, mouse.y - b.y) < ui.S(12);
    myHot = overTip || myDragging;
    if (myHot) app.shell.handleHot = true;

    const ImU32 ink = ui::ToU32(myHot ? c.sketchHover : c.accent);
    dl->AddLine(a, b, ink, ui.S(myHot ? 3.0f : 2.2f));
    const float len = std::max(std::hypot(b.x - a.x, b.y - a.y), 1.0f);
    const ImVec2 u{(b.x - a.x) / len, (b.y - a.y) / len}, n{-u.y, u.x};
    const float head = ui.S(14);
    if (len > head * 1.2f)
        dl->AddTriangleFilled(b, {b.x - u.x * head + n.x * head * 0.45f, b.y - u.y * head + n.y * head * 0.45f},
                              {b.x - u.x * head - n.x * head * 0.45f, b.y - u.y * head - n.y * head * 0.45f}, ink);
    DrawKnob(ui, dl, b, ink, myHot);
    if (myDragging) DrawValue(ui, dl, b, ink, label);

    if (!myDragging && overTip && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (const auto t = RayToLine(vf, mouse, base, dir)) {
            myDragging = true;
            myOffset = length - *t;
            myLast = length;
        }
        return std::nullopt;
    }
    if (!myDragging) return std::nullopt;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        myDragging = false;
        return std::nullopt;
    }
    const auto t = RayToLine(vf, mouse, base, dir);
    if (!t) return std::nullopt;
    const double step = DragStep(vf.projection.mmPerPixel);
    const double value = std::round((*t + myOffset) / step) * step;
    if (std::abs(value - myLast) < step * 0.5) return std::nullopt;
    myLast = value;
    return value;
}

std::optional<double> DragAngle::Update(AppContext& app, const ViewportFrame& vf, Vec3 axisPoint, Vec3 axis, Vec3 start,
                                        double from, double to, const std::string& label) {
    // The circle the start point travels on.
    const Vec3 centre = axisPoint + axis * (start - axisPoint).Dot(axis);
    const Vec3 r0 = start - centre;
    const double radius = r0.Length();
    if (radius < 1e-9) {
        myDragging = myHot = false;
        return std::nullopt;
    }
    const Vec3 e1 = r0 * (1.0 / radius), e2 = axis.Cross(e1);
    const auto at = [&](double t) { return centre + e1 * (radius * std::cos(t)) + e2 * (radius * std::sin(t)); };

    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 knob = vf.Project(at(to));
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool overKnob = vf.hovered && std::hypot(mouse.x - knob.x, mouse.y - knob.y) < ui.S(12);
    myHot = overKnob || myDragging;
    if (myHot) app.shell.handleHot = true;

    const ImU32 ink = ui::ToU32(myHot ? c.sketchHover : c.accent);
    const int segments = std::clamp(static_cast<int>(std::abs(to - from) / (kPi / 48)) + 2, 2, 200);
    std::vector<ImVec2> arc;
    arc.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) arc.push_back(vf.Project(at(from + (to - from) * i / segments)));
    dl->AddPolyline(arc.data(), static_cast<int>(arc.size()), ink, ImDrawFlags_None, ui.S(myHot ? 3.0f : 2.2f));
    // Spokes from the axis to both ends show what turns about what.
    const ImU32 faint = ui::ToU32((myHot ? c.sketchHover : c.accent).WithAlpha(0.45f));
    const ImVec2 middle = vf.Project(centre);
    dl->AddLine(middle, arc.front(), faint, ui.S(1.2f));
    dl->AddLine(middle, knob, faint, ui.S(1.2f));
    DrawKnob(ui, dl, knob, ink, myHot);
    if (myDragging) DrawValue(ui, dl, knob, ink, label);

    const auto cursorAngle = [&]() -> std::optional<double> {
        const auto hit = RayToPlane(vf, mouse, centre, axis);
        if (!hit) return std::nullopt;
        const Vec3 d = *hit - centre;
        return std::atan2(d.Dot(e2), d.Dot(e1));
    };
    if (!myDragging && overKnob && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (const auto raw = cursorAngle()) {
            myDragging = true;
            myRaw = to + std::remainder(*raw - to, 2 * kPi);
            myOffset = to - myRaw;
            myLast = to;
        }
        return std::nullopt;
    }
    if (!myDragging) return std::nullopt;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        myDragging = false;
        return std::nullopt;
    }
    const auto raw = cursorAngle();
    if (!raw) return std::nullopt;
    myRaw += std::remainder(*raw - myRaw, 2 * kPi);   // unwrapped: no jump at ±180°
    const double degree = kPi / 180.0;
    const double value = std::round((myRaw + myOffset) / degree) * degree;
    if (std::abs(value - myLast) < degree * 0.5) return std::nullopt;
    myLast = value;
    return value;
}

} // namespace bz::app
