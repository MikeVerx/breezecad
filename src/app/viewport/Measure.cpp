#include "app/viewport/Measure.h"

#include "app/Format.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <cmath>
#include <string>
#include <vector>

namespace bz::app {
namespace {

using kernel::EntityInfo;
using kernel::EntityKind;

struct Row {
    std::string label, value;
};

std::string Length(double mm, const UnitSystem& units) {
    return FormatValue(mm, Quantity::Length, units, 3) + " " + UnitLabel(Quantity::Length, units);
}

std::string Area(double mm2, const UnitSystem& units) {
    const double k = units.ToDisplay(1.0, Quantity::Length);
    return FormatGrouped(mm2 * k * k, 2) + " " + UnitLabel(Quantity::Length, units) + "\xC2\xB2";
}

/// What one picked entity is and its sizes.
void Describe(std::vector<Row>& rows, const char* title, const EntityInfo& e, const UnitSystem& units) {
    const char* kind = e.kind == EntityKind::Vertex ? Tr("вершина")
                       : e.kind == EntityKind::Edge ? (e.straight ? Tr("прямое ребро") : e.radius ? Tr("дуга / окружность") : Tr("ребро"))
                                                    : (e.flat ? Tr("плоская грань") : Tr("грань"));
    rows.push_back({title, kind});
    if (e.kind == EntityKind::Vertex) {
        rows.push_back({"X; Y; Z", FormatValue(e.point.x, Quantity::Length, units, 2) + "; " +
                                       FormatValue(e.point.y, Quantity::Length, units, 2) + "; " +
                                       FormatValue(e.point.z, Quantity::Length, units, 2)});
    }
    if (e.length) rows.push_back({Tr("Длина"), Length(*e.length, units)});
    if (e.radius) {
        rows.push_back({Tr("Радиус"), Length(*e.radius, units)});
        rows.push_back({Tr("Диаметр"), Length(*e.radius * 2, units)});
    }
    if (e.area) rows.push_back({Tr("Площадь"), Area(*e.area, units)});
}

} // namespace

void DrawMeasure(AppContext& app, Session& session, const ViewportFrame& vf) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const UnitSystem& units = session.doc.units;

    if (vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) app.viewport.MeasurePick(vf.ToLocal(ImGui::GetMousePos()));
    const render::Viewport::Measured& m = app.viewport.Measurement();

    // The shortest distance, drawn like a dimension.
    if (m.between && m.between->distance > 1e-9) {
        const ImVec2 a = vf.Project(m.between->from), b = vf.Project(m.between->to);
        dl->AddLine(a, b, ui::ToU32(c.dimension), ui.S(1.5f));
        dl->AddCircleFilled(a, ui.S(3), ui::ToU32(c.dimension));
        dl->AddCircleFilled(b, ui.S(3), ui::ToU32(c.dimension));
        const std::string text = Length(m.between->distance, units);
        const ImVec2 ts = ui.fonts.mono->CalcTextSizeA(ui.S(12.5f), FLT_MAX, 0, text.c_str());
        const ImVec2 mid{(a.x + b.x) / 2 - ts.x / 2, (a.y + b.y) / 2 - ts.y - ui.S(6)};
        dl->AddRectFilled({mid.x - ui.S(6), mid.y - ui.S(3)}, {mid.x + ts.x + ui.S(6), mid.y + ts.y + ui.S(3)},
                          ui::ToU32(c.panel.WithAlpha(0.92f)), ui.S(5));
        dl->AddText(ui.fonts.mono, ui.S(12.5f), mid, ui::ToU32(c.dimension), text.c_str());
    }

    // The card.
    std::vector<Row> rows;
    if (m.first) Describe(rows, Tr("Первый"), *m.first, units);
    if (m.second) Describe(rows, Tr("Второй"), *m.second, units);
    if (m.between) {
        rows.push_back({Tr("Расстояние"), Length(m.between->distance, units)});
        if (m.between->angle)
            rows.push_back({Tr("Угол"), FormatValue(*m.between->angle, Quantity::Angle, units, 2) + UnitLabel(Quantity::Angle, units)});
    }
    const float width = ui.S(270), pad = ui.S(14), rowH = ui.S(22);
    const char* hint = !m.first ? Tr("Щёлкните вершину, ребро или грань.") : !m.second ? Tr("Щёлкните второй элемент — покажу расстояние.")
                                                                               : Tr("Следующий щелчок начнёт новое измерение.");
    const float height = pad * 2 + ui.S(24) + rowH * static_cast<float>(rows.size()) + ui.S(22);
    const ImVec2 p{vf.min.x + ui.S(14), vf.min.y + ui.S(14)};
    dl->AddRectFilled(p, {p.x + width, p.y + height}, ui::ToU32(c.floatingButton), ui.S(10));
    dl->AddRect(p, {p.x + width, p.y + height}, ui::ToU32(c.floatingBorder), ui.S(10));
    dl->AddText(ui.fonts.strong, ui.S(13), {p.x + pad, p.y + pad}, ui::ToU32(c.text), Tr("Измерение"));
    ImGui::SetCursorScreenPos({p.x + width - ui.S(36), p.y + ui.S(8)});
    if (ui::IconButton(ui, "##measure-close", "ui-close", 26, 12, Tr("Закрыть (Esc)"))) app.shell.measuring = false;

    float y = p.y + pad + ui.S(26);
    for (const Row& r : rows) {
        dl->AddText(ui.fonts.body, ui.S(12.5f), {p.x + pad, y}, ui::ToU32(c.textSecondary), r.label.c_str());
        const ImVec2 vs = ui.fonts.mono->CalcTextSizeA(ui.S(12.5f), FLT_MAX, 0, r.value.c_str());
        dl->AddText(ui.fonts.mono, ui.S(12.5f), {p.x + width - pad - vs.x, y}, ui::ToU32(c.text), r.value.c_str());
        y += rowH;
    }
    dl->AddText(ui.fonts.caption, ui.S(11.5f), {p.x + pad, y + ui.S(2)}, ui::ToU32(c.textMuted), hint);
}

} // namespace bz::app
