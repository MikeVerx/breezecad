#include "app/viewport/Section.h"

#include "app/Format.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

constexpr std::array<const char*, 3> kPlaneLabels{"XY", "XZ", "YZ"};
constexpr std::array<const char*, 3> kAxisNames{"Z", "Y", "X"};   ///< the axis each plane moves along

/// Coordinate of `v` along the axis plane number `plane` moves along.
double Along(const Vec3& v, int plane) { return plane == 0 ? v.z : plane == 1 ? v.y : v.x; }

} // namespace

std::optional<Frame> SectionFrame(const Session& session, const ShellState::Section& section) {
    if (!section.on) return std::nullopt;
    const auto box = session.model.BoundingBox();
    if (!box) return std::nullopt;
    const int plane = std::clamp(section.plane, 0, 2);
    const Vec3 axis = plane == 0 ? Vec3{0, 0, 1} : plane == 1 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const double lo = Along(box->min, plane), hi = Along(box->max, plane);
    Frame f;
    f.normal = section.flipped ? -axis : axis;
    f.origin = axis * (lo + (hi - lo) * static_cast<double>(section.position));
    return f;
}

void DrawSectionBar(AppContext& app, Session& session, const ViewportFrame& vf) {
    ShellState::Section& s = app.shell.section;
    const auto frame = SectionFrame(session, s);
    if (!frame) return;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float segment = ui.S(132), slider = ui.S(170), label = ui.S(96), button = ui.S(30), gap = ui.S(8), pad = ui.S(8);
    const float width = pad * 2 + segment + slider + label + button * 2 + gap * 4;
    const float height = ui.S(44);
    const ImVec2 p{(vf.min.x + vf.max.x - width) / 2, vf.min.y + ui.S(14)};
    dl->AddRectFilled(p, {p.x + width, p.y + height}, ui::ToU32(c.floatingButton), ui.S(10));
    dl->AddRect(p, {p.x + width, p.y + height}, ui::ToU32(c.floatingBorder), ui.S(10));

    float x = p.x + pad;
    const float y = p.y + (height - ui.S(34)) / 2;
    ImGui::SetCursorScreenPos({x, y});
    const int chosen = ui::Segmented(ui, "section-plane", kPlaneLabels, s.plane, 3, segment);
    if (chosen != s.plane) s.plane = chosen;
    x += segment + gap;

    ImGui::SetCursorScreenPos({x, p.y + (height - ui.S(28)) / 2});
    ui::Slider(ui, "##section-position", s.position, slider);
    x += slider + gap;

    const std::string text = std::string(kAxisNames[static_cast<std::size_t>(s.plane)]) + " = " +
                             FormatValue(Along(frame->origin, s.plane), Quantity::Length, session.doc.units, 2) + " " +
                             UnitLabel(Quantity::Length, session.doc.units);
    dl->AddText(ui.fonts.mono, ui.S(12.5f), {x, p.y + (height - ui.S(12.5f)) / 2}, ui::ToU32(c.text), text.c_str());
    x += label + gap;

    ImGui::SetCursorScreenPos({x, p.y + (height - button) / 2});
    if (ui::IconButton(ui, "##section-flip", "ui-swap", 30, 16, Tr("Показать другую половину"))) s.flipped = !s.flipped;
    x += button + gap;
    ImGui::SetCursorScreenPos({x, p.y + (height - button) / 2});
    if (ui::IconButton(ui, "##section-close", "ui-close", 30, 14, Tr("Убрать сечение"))) s.on = false;
}

} // namespace bz::app
