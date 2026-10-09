#include "app/features/DragArrow.h"

#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

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
    const float radius = ui.S(myHot ? 7.5f : 6.0f);
    dl->AddCircleFilled(b, radius, ui::ToU32(c.panel));
    dl->AddCircle(b, radius, ink, 24, ui.S(2.2f));
    if (myDragging && !label.empty()) {
        // On a plate: the value stays readable over the body.
        const float size = ui.S(13);
        const ImVec2 extent = ui.fonts.mono->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
        const ImVec2 at{b.x + ui.S(14), b.y - ui.S(22)};
        const float pad = ui.S(4);
        dl->AddRectFilled({at.x - pad, at.y - pad}, {at.x + extent.x + pad, at.y + extent.y + pad}, ui::ToU32(c.panel), ui.S(4));
        dl->AddRect({at.x - pad, at.y - pad}, {at.x + extent.x + pad, at.y + extent.y + pad}, ink, ui.S(4));
        dl->AddText(ui.fonts.mono, size, at, ui::ToU32(c.text), label.c_str());
    }

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

} // namespace bz::app
