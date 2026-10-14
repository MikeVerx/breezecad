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

/// "вершина", "прямое ребро"… — what the field shows.
std::string KindOf(const EntityInfo& e) {
    return e.kind == EntityKind::Vertex ? Tr("вершина")
           : e.kind == EntityKind::Edge ? (e.straight ? Tr("прямое ребро") : e.radius ? Tr("дуга / окружность") : Tr("ребро"))
                                        : (e.flat ? Tr("плоская грань") : Tr("грань"));
}

/// The sizes of one picked entity.
void Describe(std::vector<Row>& rows, const std::string& prefix, const EntityInfo& e, const UnitSystem& units) {
    if (e.kind == EntityKind::Vertex) {
        rows.push_back({prefix + "X; Y; Z", FormatValue(e.point.x, Quantity::Length, units, 2) + "; " +
                                                FormatValue(e.point.y, Quantity::Length, units, 2) + "; " +
                                                FormatValue(e.point.z, Quantity::Length, units, 2)});
    }
    if (e.length) rows.push_back({prefix + Tr("Длина"), Length(*e.length, units)});
    if (e.radius) {
        rows.push_back({prefix + Tr("Радиус"), Length(*e.radius, units)});
        rows.push_back({prefix + Tr("Диаметр"), Length(*e.radius * 2, units)});
    }
    if (e.area) rows.push_back({prefix + Tr("Площадь"), Area(*e.area, units)});
}

/// Two flat faces or straight edges at an angle: the angle is the answer
/// (as in Inventor); the distance between their nearest corners is not.
bool Angled(const render::Viewport::Measured& m) { return m.between && m.between->angle && *m.between->angle > 1e-6; }

std::string AngleText(double radians, const UnitSystem& units) {
    return FormatValue(radians, Quantity::Angle, units, 2) + UnitLabel(Quantity::Angle, units);
}

/// Every number, in the order the panel shows them.
std::vector<Row> Results(const render::Viewport::Measured& m, const UnitSystem& units) {
    std::vector<Row> rows;
    if (m.between) {
        if (Angled(m)) {
            rows.push_back({Tr("Угол"), AngleText(*m.between->angle, units)});
            rows.push_back({Tr("Наименьшее расстояние"), Length(m.between->distance, units)});
        } else {
            rows.push_back({Tr("Расстояние"), Length(m.between->distance, units)});
            const Vec3 d = m.between->to - m.between->from;
            rows.push_back({"\xCE\x94X", Length(std::abs(d.x), units)});   // ΔX
            rows.push_back({"\xCE\x94Y", Length(std::abs(d.y), units)});
            rows.push_back({"\xCE\x94Z", Length(std::abs(d.z), units)});
            if (m.between->angle) rows.push_back({Tr("Угол"), AngleText(*m.between->angle, units)});
        }
    }
    const bool both = m.first && m.second;
    if (m.first) Describe(rows, both ? "1: " : "", *m.first, units);
    if (m.second) Describe(rows, both ? "2: " : "", *m.second, units);
    return rows;
}

} // namespace

void DrawMeasure(AppContext& app, Session& session, const ViewportFrame& vf) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const UnitSystem& units = session.doc.units;

    int& slot = app.shell.measureSlot;
    if (vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        app.viewport.MeasurePick(vf.ToLocal(ImGui::GetMousePos()), slot)) {
        // The other field next while it is empty; with both filled, the
        // next click replaces the second (the first is the reference).
        const render::Viewport::Measured& now = app.viewport.Measurement();
        if (slot == 0 && !now.second) slot = 1;
        else if (slot == 1 && !now.first) slot = 0;
        else slot = 1;
    }
    const render::Viewport::Measured& m = app.viewport.Measurement();

    // On the model: the shortest distance drawn like a dimension, with a
    // short label (the angle for two faces or edges at an angle).
    const bool angled = Angled(m);
    if (m.between && (m.between->distance > 1e-9 || angled)) {
        const ImVec2 a = vf.Project(m.between->from), b = vf.Project(m.between->to);
        if (!angled) {
            dl->AddLine(a, b, ui::ToU32(c.dimension), ui.S(1.5f));
            dl->AddCircleFilled(a, ui.S(3), ui::ToU32(c.dimension));
            dl->AddCircleFilled(b, ui.S(3), ui::ToU32(c.dimension));
        }
        const std::string text = angled ? AngleText(*m.between->angle, units) : Length(m.between->distance, units);
        const ImVec2 ts = ui.fonts.mono->CalcTextSizeA(ui.S(12.5f), FLT_MAX, 0, text.c_str());
        const ImVec2 mid{(a.x + b.x) / 2 - ts.x / 2, (a.y + b.y) / 2 - ts.y - ui.S(6)};
        dl->AddRectFilled({mid.x - ui.S(6), mid.y - ui.S(3)}, {mid.x + ts.x + ui.S(6), mid.y + ts.y + ui.S(3)},
                          ui::ToU32(c.panel.WithAlpha(0.92f)), ui.S(5));
        dl->AddText(ui.fonts.mono, ui.S(12.5f), mid, ui::ToU32(c.dimension), text.c_str());
    }
}

void DrawMeasurePanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const UnitSystem& units = session.doc.units;
    const render::Viewport::Measured& m = app.viewport.Measurement();
    int& slot = app.shell.measureSlot;

    // Header as the operation panels have it: icon, title, close.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        ui.icons.Draw(dl, "measure", {p.x + ui.S(14), p.y + (h - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
        dl->AddText(ui.fonts.title, ui.S(14), {p.x + ui.S(48), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, ui::ToU32(c.text), Tr("Измерение"));
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##measure-close", "ui-close", 30, 14, Tr("Закончить (Esc)"))) app.shell.measuring = false;
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ui::Divider(ui, c.dividerSoft);
    }
    ImGui::SetCursorPos({ui.S(14), ImGui::GetCursorPosY() + ui.S(14)});
    ImGui::BeginGroup();
    const float inner = width - ui.S(28);

    ui::FieldLabel(ui, Tr("Что измерить"));
    bool clear0 = false, clear1 = false;
    const std::string first = m.first ? KindOf(*m.first) : std::string(), second = m.second ? KindOf(*m.second) : std::string();
    const char* waiting = Tr("ожидает выбора…");
    if (ui::SelectionSlot(ui, "##measure-1", Tr("Элемент 1"), first, waiting, slot == 0, inner, &clear0)) slot = 0;
    if (ui::SelectionSlot(ui, "##measure-2", Tr("Элемент 2"), second, waiting, slot == 1, inner, &clear1)) slot = 1;
    if (clear0) {
        app.viewport.ClearMeasure(0);
        slot = 0;
    }
    if (clear1) {
        app.viewport.ClearMeasure(1);
        slot = 1;
    }
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
        ImGui::TextUnformatted(Tr("Щёлкните вершину, ребро или грань — она встанет в выделенное поле. Щелчок по полю выбирает, "
                                  "что заменить; промах ничего не сбрасывает."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    const std::vector<Row> rows = Results(m, units);
    if (!rows.empty()) {
        ImGui::Dummy({0, ui.S(10)});
        ui::FieldLabel(ui, Tr("Результат"));
        for (const Row& r : rows) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ui.S(24);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ui.fonts.body, ui.S(12.5f), {p.x, p.y + ui.S(4)}, ui::ToU32(c.textSecondary), r.label.c_str());
            const ImVec2 vs = ui.fonts.mono->CalcTextSizeA(ui.S(12.5f), FLT_MAX, 0, r.value.c_str());
            dl->AddText(ui.fonts.mono, ui.S(12.5f), {p.x + inner - vs.x, p.y + ui.S(4)}, ui::ToU32(c.text), r.value.c_str());
            ImGui::Dummy({inner, h});
        }
        ImGui::Dummy({0, ui.S(6)});
        if (ui::SecondaryButton(ui, "##measure-copy", Tr("Копировать"), nullptr, {ui.S(140), ui.S(32)})) {
            std::string text;
            for (const Row& r : rows) text += r.label + ": " + r.value + "\n";
            ImGui::SetClipboardText(text.c_str());
            app.notes.Success(Tr("Результат скопирован"));
        }
    }
    ImGui::EndGroup();
}

} // namespace bz::app
