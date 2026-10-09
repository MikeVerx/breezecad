// The left-area panel of a drawing tool, like KOMPAS's parameters panel: a
// row of ways to draw and fields for exact sizes. A typed size locks the
// cursor while drawing and becomes a dimension of the new shape.

#include "app/Format.h"
#include "core/Expression.h"
#include "app/sketch/SketchEditor.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bz::app {
namespace {

struct Way {
    const char* icon;
    const char* tip;
};

/// A row of icon buttons; returns the chosen index.
int WayRow(ui::Ui& ui, const char* id, std::span<const Way> ways, int current) {
    int chosen = current;
    for (std::size_t i = 0; i < ways.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, ui.S(6));
        ImGui::PushID(id);
        ImGui::PushID(static_cast<int>(i));
        if (ui::IconButton(ui, "##way", ways[i].icon, 40, 24, ways[i].tip, true, static_cast<int>(i) == current))
            chosen = static_cast<int>(i);
        ImGui::PopID();
        ImGui::PopID();
    }
    return chosen;
}

void Caption(ui::Ui& ui, const char* text, float width) {
    ui::FontScope font(ui.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

/// A typed number of any sign; nullopt when empty or not a plain value.
std::optional<double> PlainValue(const std::string& text, Quantity quantity, const UnitSystem& units) {
    if (text.empty()) return std::nullopt;
    const Expected<double> v = EvaluateExpression(text, quantity, units, [](std::string_view) { return std::optional<NamedValue>{}; });
    return v ? std::optional<double>(*v) : std::nullopt;
}

} // namespace

bool SketchEditor::HasToolPanel() const {
    return myTool == SketchTool::Line || myTool == SketchTool::Rectangle || myTool == SketchTool::Circle ||
           myTool == SketchTool::Arc || myTool == SketchTool::Move || myTool == SketchTool::Rotate;
}

void SketchEditor::DrawToolPanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const UnitSystem& units = session.doc.units;
    const bool polygon = myTool == SketchTool::Circle && myOptions.circle == CircleMode::Polygon;

    // Header: tool icon and name, close returns to selection.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        const SketchToolInfo& info = InfoOf(myTool);
        ui.icons.Draw(dl, info.icon, {p.x + ui.S(14), p.y + (h - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
        const std::string title = std::string("Эскиз › ") + (polygon ? "Многоугольник" : info.label);
        dl->AddText(ui.fonts.title, ui.S(14), {p.x + ui.S(48), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, ui::ToU32(c.text),
                    title.c_str());
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##close-tool", "ui-close", 30, 14, "Закончить (Esc)"))
            app.Later([&app, &session, this] { SetTool(app, session, SketchTool::Select); });
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ui::Divider(ui, c.dividerSoft);
    }

    ImGui::SetCursorPos({ui.S(14), ImGui::GetCursorPosY() + ui.S(14)});
    ImGui::BeginGroup();
    const float inner = width - ui.S(28);
    const auto restart = [this] { myClicks.clear(); };
    const auto field = [&](const char* label, const char* id, std::string& text, Quantity q) {
        ui::FieldLabel(ui, label);
        ImGui::Dummy({0, ui.S(2)});
        ui::ExpressionField(ui, id, text, UnitLabel(q, units), {}, inner, false, false);
        ImGui::Dummy({0, ui.S(8)});
    };

    // Ways to draw.
    switch (myTool) {
        case SketchTool::Rectangle: {
            static constexpr std::array<Way, 3> ways{{{"sk-rectangle", "По двум углам"},
                                                     {"sk-rect-centre", "От центра"},
                                                     {"sk-rect-3pt", "По трём точкам (под углом)"}}};
            const int chosen = WayRow(ui, "rect", ways, static_cast<int>(myOptions.rectangle));
            if (chosen != static_cast<int>(myOptions.rectangle)) {
                myOptions.rectangle = static_cast<RectangleMode>(chosen);
                restart();
            }
            break;
        }
        case SketchTool::Circle: {
            static constexpr std::array<Way, 3> ways{{{"sk-circle", "Центр и радиус"},
                                                     {"sk-circle-3pt", "По трём точкам"},
                                                     {"sk-polygon", "Многоугольник"}}};
            const int chosen = WayRow(ui, "circle", ways, static_cast<int>(myOptions.circle));
            if (chosen != static_cast<int>(myOptions.circle)) {
                myOptions.circle = static_cast<CircleMode>(chosen);
                restart();
            }
            break;
        }
        case SketchTool::Arc: {
            static constexpr std::array<Way, 2> ways{{{"sk-arc", "Центр, начало, конец"}, {"sk-arc-3pt", "По трём точкам"}}};
            const int chosen = WayRow(ui, "arc", ways, static_cast<int>(myOptions.arc));
            if (chosen != static_cast<int>(myOptions.arc)) {
                myOptions.arc = static_cast<ArcMode>(chosen);
                restart();
            }
            break;
        }
        default: break;
    }
    if (myTool != SketchTool::Line) ImGui::Dummy({0, ui.S(14)});

    // Sizes.
    switch (myTool) {
        case SketchTool::Line:
            field("Длина", "##len", myOptions.length, Quantity::Length);
            field("Угол", "##ang", myOptions.angle, Quantity::Angle);
            break;
        case SketchTool::Rectangle:
            field(myOptions.rectangle == RectangleMode::ThreePoints ? "Длина первой стороны" : "Ширина", "##w",
                  myOptions.width, Quantity::Length);
            field(myOptions.rectangle == RectangleMode::ThreePoints ? "Вторая сторона" : "Высота", "##h", myOptions.height,
                  Quantity::Length);
            break;
        case SketchTool::Circle:
            if (polygon) {
                ui::FieldLabel(ui, "Число сторон");
                ImGui::Dummy({0, ui.S(2)});
                const ui::FieldResult r = ui::ExpressionField(ui, "##sides", myOptions.sides, "", mySidesError, inner, false, false);
                if (r.edited || r.committed) {
                    const Expected<double> n = EvaluateExpression(myOptions.sides, Quantity::Scalar, units,
                                                                  [](std::string_view) { return std::optional<NamedValue>{}; });
                    const bool whole = n && std::abs(*n - std::round(*n)) < 1e-9 && *n >= 3 && *n <= 200;
                    mySidesError = whole ? std::string() : std::string("Целое число от 3 до 200");
                    if (whole) myOptions.polygonSides = static_cast<int>(std::lround(*n));
                }
                ImGui::Dummy({0, ui.S(8)});
                static constexpr std::array<const char*, 2> kinds{"Вписанный", "Описанный"};
                const int kind = myOptions.polygonInscribed ? 0 : 1;
                if (const int k = ui::Segmented(ui, "kind", kinds, kind, 2, inner); k != kind) {
                    myOptions.polygonInscribed = k == 0;
                    restart();
                }
                ImGui::Dummy({0, ui.S(8)});
                field("Диаметр окружности", "##d", myOptions.size, Quantity::Length);
            } else if (myOptions.circle == CircleMode::CentreRadius) {
                field("Диаметр", "##d", myOptions.size, Quantity::Length);
            }
            break;
        case SketchTool::Arc:
            if (myOptions.arc == ArcMode::CentreEnds) field("Радиус", "##r", myOptions.size, Quantity::Length);
            break;
        case SketchTool::Move:
        case SketchTool::Rotate: {
            const std::string chosen = mySelection.empty() ? std::string("Ничего не выбрано")
                                                           : "Выбрано элементов: " + std::to_string(mySelection.size());
            ui::FieldLabel(ui, chosen.c_str());
            ImGui::Dummy({0, ui.S(10)});
            if (myTool == SketchTool::Move) {
                field("Сдвиг по X", "##dx", myOptions.moveX, Quantity::Length);
                field("Сдвиг по Y", "##dy", myOptions.moveY, Quantity::Length);
                const auto dx = PlainValue(myOptions.moveX, Quantity::Length, units);
                const auto dy = PlainValue(myOptions.moveY, Quantity::Length, units);
                const bool ready = !mySelection.empty() && (dx || dy);
                if (ui::PrimaryButton(ui, "##move-now", "Сдвинуть", nullptr, {inner, ui.S(34)}, ready))
                    app.Later([&app, &session, this, dx, dy] {
                        ApplyMotion(app, session, {{}, 0, {dx.value_or(0), dy.value_or(0)}});
                    });
                ImGui::Dummy({0, ui.S(10)});
            } else {
                field("Угол", "##turn", myOptions.turn, Quantity::Angle);
            }
            break;
        }
        default: break;
    }

    const bool sized = myTool == SketchTool::Line || myTool == SketchTool::Rectangle ||
                       (myTool == SketchTool::Circle && myOptions.circle != CircleMode::ThreePoints) ||
                       (myTool == SketchTool::Arc && myOptions.arc == ArcMode::CentreEnds);
    if (myTool == SketchTool::Move) {
        Caption(ui, "Ctrl+щелчок добавляет или убирает элемент. Затем базовая точка и новое место — или введите сдвиг.", inner);
        ImGui::EndGroup();
        return;
    }
    if (myTool == SketchTool::Rotate) {
        Caption(ui, "Ctrl+щелчок добавляет или убирает элемент. С углом: щёлкните центр. Без угла: центр, откуда и куда.", inner);
        ImGui::EndGroup();
        return;
    }
    Caption(ui,
            sized ? "Введённый размер фиксирует курсор и станет размером эскиза. Пустое поле — размер задаётся мышью."
                  : "Укажите три точки в поле эскиза.",
            inner);
    ImGui::EndGroup();
}

} // namespace bz::app
