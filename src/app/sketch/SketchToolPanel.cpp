// The left-area panel of a drawing tool, like KOMPAS's parameters panel: a
// row of ways to draw and fields for exact sizes. A typed size locks the
// cursor while drawing and becomes a dimension of the new shape.

#include "app/Format.h"
#include "app/sketch/SketchEditor.h"
#include "core/Expression.h"
#include "core/I18n.h"
#include "kernel/TextGeometry.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <imgui_stdlib.h>

#include <array>
#include <cmath>

namespace bz::app {
namespace {

struct Way {
    const char* icon;
    const char* tip;
};

/// Icon buttons in rows that fit `width`; returns the chosen index.
int WayRow(ui::Ui& ui, const char* id, std::span<const Way> ways, int current, float width) {
    int chosen = current;
    const float cell = ui.S(40), gap = ui.S(6);
    const std::size_t perRow = std::max<std::size_t>(1, static_cast<std::size_t>((width + gap) / (cell + gap)));
    for (std::size_t i = 0; i < ways.size(); ++i) {
        if (i % perRow != 0) ImGui::SameLine(0, gap);
        ImGui::PushID(id);
        ImGui::PushID(static_cast<int>(i));
        if (ui::IconButton(ui, "##way", ways[i].icon, 40, 24, Tr(ways[i].tip), true, static_cast<int>(i) == current))
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
    if (myTool >= SketchTool::Coincident) return true;   // constraints: the picked elements
    return myTool == SketchTool::Line || myTool == SketchTool::Rectangle || myTool == SketchTool::Circle ||
           myTool == SketchTool::Arc || myTool == SketchTool::Move || myTool == SketchTool::Rotate || myTool == SketchTool::Mirror ||
           myTool == SketchTool::Copy || myTool == SketchTool::Scale || myTool == SketchTool::PatternRect ||
           myTool == SketchTool::PatternCircular || myTool == SketchTool::Fillet || myTool == SketchTool::Offset ||
           myTool == SketchTool::Spline || myTool == SketchTool::Text;
}

void SketchEditor::DrawToolPanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const UnitSystem& units = session.doc.units;
    const bool polygon = myTool == SketchTool::Rectangle && myOptions.rectangle == RectangleMode::Polygon;

    // Header: tool icon and name, close returns to selection.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        const SketchToolInfo& info = InfoOf(myTool);
        ui.icons.Draw(dl, info.icon, {p.x + ui.S(14), p.y + (h - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
        const bool slot = myTool == SketchTool::Rectangle && IsSlot(myOptions.rectangle);
        const std::string title = Trf("Эскиз › {}", polygon ? Tr("Многоугольник") : slot ? Tr("Паз") : Tr(info.label));
        dl->AddText(ui.fonts.title, ui.S(14), {p.x + ui.S(48), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, ui::ToU32(c.text),
                    title.c_str());
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##close-tool", "ui-close", 30, 14, Tr("Закончить (Esc)")))
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

    if (myTool >= SketchTool::Coincident) {
        DrawPicks(app, session, inner);
        ImGui::EndGroup();
        return;
    }

    // Line type: осевая only for lines, the others take основная / вспомогательная.
    const bool editing = myTool == SketchTool::Move || myTool == SketchTool::Rotate || myTool == SketchTool::Mirror ||
                         myTool == SketchTool::Copy || myTool == SketchTool::Scale || myTool == SketchTool::PatternRect ||
                         myTool == SketchTool::PatternCircular;
    if (!editing) {
        const bool lines = myTool == SketchTool::Line;
        if (!lines && myOptions.style == LineStyle::Centerline) myOptions.style = LineStyle::Normal;
        ui::FieldLabel(ui, Tr("Тип линии"));
        ImGui::Dummy({0, ui.S(2)});
        static constexpr std::array<const char*, 3> styles{N_("Основная"), N_("Вспомогат."), N_("Осевая")};
        const int current = static_cast<int>(myOptions.style);
        const int chosen = ui::Segmented(ui, "line-style", styles, current, 3, inner, lines ? 0 : 0b100);
        if (chosen != current) myOptions.style = static_cast<LineStyle>(chosen);
        ImGui::Dummy({0, ui.S(14)});
    }

    // Ways to draw.
    switch (myTool) {
        case SketchTool::Rectangle: {
            static constexpr std::array<Way, 10> ways{{{"sk-rectangle", N_("Прямоугольник по двум углам")},
                                                      {"sk-rect-centre", N_("Прямоугольник от центра")},
                                                      {"sk-rect-3pt", N_("Прямоугольник по трём точкам (под углом)")},
                                                      {"sk-rect-3pt-centre", N_("Прямоугольник по трём точкам и центру")},
                                                      {"sk-polygon", N_("Многоугольник")},
                                                      {"sk-slot-centres", N_("Паз: от центра до центра")},
                                                      {"sk-slot-overall", N_("Паз: по краям")},
                                                      {"sk-slot-centre", N_("Паз: от середины")},
                                                      {"sk-slot-arc3", N_("Дуговой паз по трём точкам")},
                                                      {"sk-slot-arc-centre", N_("Дуговой паз от центра дуги")}}};
            const int chosen = WayRow(ui, "rect", ways, static_cast<int>(myOptions.rectangle), inner);
            if (chosen != static_cast<int>(myOptions.rectangle)) {
                myOptions.rectangle = static_cast<RectangleMode>(chosen);
                restart();
            }
            break;
        }
        case SketchTool::Circle: {
            static constexpr std::array<Way, 2> ways{{{"sk-circle", N_("Центр и радиус")}, {"sk-circle-3pt", N_("По трём точкам")}}};
            const int chosen = WayRow(ui, "circle", ways, static_cast<int>(myOptions.circle), inner);
            if (chosen != static_cast<int>(myOptions.circle)) {
                myOptions.circle = static_cast<CircleMode>(chosen);
                restart();
            }
            break;
        }
        case SketchTool::Spline: {
            static constexpr std::array<Way, 2> ways{{{"sk-spline", N_("Через точки")}, {"sk-spline-cv", N_("По управляющим точкам")}}};
            const int current = myOptions.splineControl ? 1 : 0;
            if (const int chosen = WayRow(ui, "spline", ways, current, inner); chosen != current) {
                myOptions.splineControl = chosen == 1;
                restart();
            }
            break;
        }
        case SketchTool::Arc: {
            static constexpr std::array<Way, 2> ways{{{"sk-arc", N_("Центр, начало, конец")}, {"sk-arc-3pt", N_("По трём точкам")}}};
            const int chosen = WayRow(ui, "arc", ways, static_cast<int>(myOptions.arc), inner);
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
            field(Tr("Длина"), "##len", myOptions.length, Quantity::Length);
            field(Tr("Угол"), "##ang", myOptions.angle, Quantity::Angle);
            break;
        case SketchTool::Rectangle:
            if (IsSlot(myOptions.rectangle)) {
                field(Tr("Ширина паза"), "##w", myOptions.width, Quantity::Length);
            } else if (myOptions.rectangle == RectangleMode::ThreePointsCentre) {
                // sizes by clicks; dimension afterwards
            } else if (polygon) {
                ui::FieldLabel(ui, Tr("Число сторон"));
                ImGui::Dummy({0, ui.S(2)});
                const ui::FieldResult r = ui::ExpressionField(ui, "##sides", myOptions.sides, "", mySidesError, inner, false, false);
                if (r.edited || r.committed) {
                    const Expected<double> n = EvaluateExpression(myOptions.sides, Quantity::Scalar, units,
                                                                  [](std::string_view) { return std::optional<NamedValue>{}; });
                    const bool whole = n && std::abs(*n - std::round(*n)) < 1e-9 && *n >= 3 && *n <= 200;
                    mySidesError = whole ? std::string() : std::string(Tr("Целое число от 3 до 200"));
                    if (whole) myOptions.polygonSides = static_cast<int>(std::lround(*n));
                }
                ImGui::Dummy({0, ui.S(8)});
                static constexpr std::array<const char*, 2> kinds{N_("Вписанный"), N_("Описанный")};
                const int kind = myOptions.polygonInscribed ? 0 : 1;
                if (const int k = ui::Segmented(ui, "kind", kinds, kind, 2, inner); k != kind) {
                    myOptions.polygonInscribed = k == 0;
                    restart();
                }
                ImGui::Dummy({0, ui.S(8)});
                field(Tr("Диаметр окружности"), "##d", myOptions.size, Quantity::Length);
            } else {
                field(myOptions.rectangle == RectangleMode::ThreePoints ? Tr("Длина первой стороны") : Tr("Ширина"), "##w",
                      myOptions.width, Quantity::Length);
                field(myOptions.rectangle == RectangleMode::ThreePoints ? Tr("Вторая сторона") : Tr("Высота"), "##h",
                      myOptions.height, Quantity::Length);
            }
            break;
        case SketchTool::Circle:
            if (myOptions.circle == CircleMode::CentreRadius) field(Tr("Диаметр"), "##d", myOptions.size, Quantity::Length);
            break;
        case SketchTool::Arc:
            if (myOptions.arc == ArcMode::CentreEnds) field(Tr("Радиус"), "##r", myOptions.size, Quantity::Length);
            break;
        case SketchTool::Fillet:
            field(Tr("Радиус"), "##fillet", myOptions.filletRadius, Quantity::Length);
            break;
        case SketchTool::Text: {
            if (myEditedText) Caption(ui, Tr("Правка надписи: изменения видны сразу. Esc — закончить."), inner);
            ui::FieldLabel(ui, Tr("Текст"));
            ImGui::SetNextItemWidth(inner);
            ImGui::InputTextMultiline("##text", &myOptions.text, {inner, ui.S(64)});
            ImGui::Dummy({0, ui.S(6)});
            field(Tr("Высота"), "##text-height", myOptions.textHeight, Quantity::Length);
            ui::FieldLabel(ui, Tr("Шрифт"));
            ImGui::SetNextItemWidth(inner);
            const std::string shown = myOptions.textFont.empty() ? std::string(Tr("Шрифт программы")) : myOptions.textFont;
            if (ImGui::BeginCombo("##font", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
                if (ImGui::Selectable(Tr("Шрифт программы"), myOptions.textFont.empty())) myOptions.textFont.clear();
                for (const std::string& name : kernel::AvailableFonts())
                    if (ImGui::Selectable(name.c_str(), name == myOptions.textFont)) myOptions.textFont = name;
                ImGui::EndCombo();
            }
            ImGui::Dummy({0, ui.S(6)});
            static constexpr std::array<const char*, 4> styles{N_("Обычный"), N_("Жирный"), N_("Курсив"), N_("Жирн. курсив")};
            myOptions.textStyle = ui::Segmented(ui, "text-style", styles, myOptions.textStyle, 2, inner);
            ImGui::Dummy({0, ui.S(6)});
            ui::FieldLabel(ui, Tr("Выравнивание в рамке"));
            static constexpr std::array<const char*, 3> across{N_("Влево"), N_("По центру"), N_("Вправо")};
            myOptions.textHAlign = ui::Segmented(ui, "text-h", across, myOptions.textHAlign, 3, inner);
            ImGui::Dummy({0, ui.S(4)});
            static constexpr std::array<const char*, 3> down{N_("Вниз"), N_("Посередине"), N_("Вверх")};
            myOptions.textVAlign = ui::Segmented(ui, "text-v", down, myOptions.textVAlign, 3, inner);
            Caption(ui, Tr("Щелчок ставит рамку по размеру текста, протяжка задаёт её размер. Рамку можно образмерить и повернуть (уберите её горизонтальность)."), inner);
            ApplyTextOptions(session);   // the edited text follows the panel at once
            break;
        }
        case SketchTool::Offset:
            field(Tr("Расстояние"), "##offset", myOptions.offsetDistance, Quantity::Length);
            break;
        case SketchTool::Mirror: {
            const std::string chosen = mySelection.empty() ? std::string(Tr("Ничего не выбрано"))
                                                           : Trf("Выбрано элементов: {}", mySelection.size());
            ui::FieldLabel(ui, chosen.c_str());
            ImGui::Dummy({0, ui.S(6)});
            static constexpr std::array<const char*, 2> steps{N_("Элементы"), N_("Линия зеркала")};
            const int current = myMirrorAxisStep ? 1 : 0;
            const int chosenStep = ui::Segmented(ui, "mirror-step", steps, current, 2, inner, mySelection.empty() ? 0b10 : 0);
            if (chosenStep != current) myMirrorAxisStep = chosenStep == 1;
            ImGui::Dummy({0, ui.S(10)});
            Caption(ui,
                    myMirrorAxisStep ? Tr("Щёлкните отрезок — линию зеркала. Копии останутся симметричны оригиналу.")
                                     : Tr("Щёлкайте элементы, которые нужно отразить (повторный щелчок убирает), затем «Линия зеркала»."),
                    inner);
            ImGui::EndGroup();
            return;
        }
        case SketchTool::Scale:
        case SketchTool::PatternRect:
        case SketchTool::PatternCircular: {
            const std::string chosen = mySelection.empty() ? std::string(Tr("Ничего не выбрано — щёлкайте элементы"))
                                                           : Trf("Выбрано элементов: {}", mySelection.size());
            ui::FieldLabel(ui, chosen.c_str());
            ImGui::Dummy({0, ui.S(10)});
            bool ready = !mySelection.empty();
            const char* button = Tr("Создать");
            if (myTool == SketchTool::Scale) {
                field(Tr("Коэффициент"), "##k", myOptions.factor, Quantity::Scalar);
                ready = ready && !myClicks.empty();
                button = Tr("Масштабировать");
                Caption(ui, myClicks.empty() ? Tr("Щёлкните базовую точку — она останется на месте.") : Tr("Базовая точка выбрана (щелчок — другая)."), inner);
            } else if (myTool == SketchTool::PatternRect) {
                static constexpr std::array<const char*, 2> dirs{N_("Вдоль X"), N_("Вдоль Y")};
                const int current = myOptions.vertical1 ? 1 : 0;
                if (const int d = ui::Segmented(ui, "dir", dirs, current, 2, inner); d != current) myOptions.vertical1 = d == 1;
                ImGui::Dummy({0, ui.S(8)});
                field(Tr("Количество"), "##n1", myOptions.count1, Quantity::Scalar);
                field(Tr("Шаг"), "##s1", myOptions.spacing1, Quantity::Length);
                field(Tr("Количество во втором направлении"), "##n2", myOptions.count2, Quantity::Scalar);
                field(Tr("Шаг во втором направлении"), "##s2", myOptions.spacing2, Quantity::Length);
            } else {
                field(Tr("Количество"), "##n", myOptions.count1, Quantity::Scalar);
                field(Tr("Угол"), "##sweep", myOptions.sweep, Quantity::Angle);
                ready = ready && !myClicks.empty();
                Caption(ui, myClicks.empty() ? Tr("Щёлкните центр массива.") : Tr("Центр выбран (щелчок — другой). 360° — равномерно по кругу."), inner);
            }
            ImGui::Dummy({0, ui.S(8)});
            if (myTool != SketchTool::Scale) ready = ready && !PatternMotions(units).empty();
            if (ui::PrimaryButton(ui, "##apply-edit", button, nullptr, {inner, ui.S(34)}, ready))
                app.Later([&app, &session, this] {
                    if (myTool == SketchTool::Scale) ApplyScale(app, session);
                    else ApplyPattern(app, session);
                });
            ImGui::EndGroup();
            return;
        }
        case SketchTool::Copy:
        case SketchTool::Move:
        case SketchTool::Rotate: {
            const std::string chosen = mySelection.empty() ? std::string(Tr("Ничего не выбрано"))
                                                           : Trf("Выбрано элементов: {}", mySelection.size());
            ui::FieldLabel(ui, chosen.c_str());
            ImGui::Dummy({0, ui.S(10)});
            if (myTool == SketchTool::Move || myTool == SketchTool::Copy) {
                field(Tr("Сдвиг по X"), "##dx", myOptions.moveX, Quantity::Length);
                field(Tr("Сдвиг по Y"), "##dy", myOptions.moveY, Quantity::Length);
                const auto dx = PlainValue(myOptions.moveX, Quantity::Length, units);
                const auto dy = PlainValue(myOptions.moveY, Quantity::Length, units);
                const bool ready = !mySelection.empty() && (dx || dy);
                if (ui::PrimaryButton(ui, "##move-now", myTool == SketchTool::Copy ? Tr("Копировать") : Tr("Сдвинуть"), nullptr,
                                      {inner, ui.S(34)}, ready))
                    app.Later([&app, &session, this, dx, dy] {
                        ApplyMotion(app, session, {{}, 0, {dx.value_or(0), dy.value_or(0)}});
                    });
                ImGui::Dummy({0, ui.S(10)});
            } else {
                field(Tr("Угол"), "##turn", myOptions.turn, Quantity::Angle);
            }
            break;
        }
        default: break;
    }

    const bool sized = myTool == SketchTool::Line || myTool == SketchTool::Rectangle ||
                       (myTool == SketchTool::Circle && myOptions.circle != CircleMode::ThreePoints) ||
                       (myTool == SketchTool::Arc && myOptions.arc == ArcMode::CentreEnds);
    if (myTool == SketchTool::Move || myTool == SketchTool::Copy) {
        Caption(ui, Tr("Ctrl+щелчок добавляет или убирает элемент. Затем базовая точка и новое место — или введите сдвиг."), inner);
        ImGui::EndGroup();
        return;
    }
    if (myTool == SketchTool::Rotate) {
        Caption(ui, Tr("Ctrl+щелчок добавляет или убирает элемент. С углом: щёлкните центр. Без угла: центр, откуда и куда."), inner);
        ImGui::EndGroup();
        return;
    }
    // Shapes only: the other tools say what to do themselves.
    const bool shape = myTool == SketchTool::Line || myTool == SketchTool::Rectangle || myTool == SketchTool::Circle ||
                       myTool == SketchTool::Arc;
    if (shape)
        Caption(ui,
                sized ? Tr("Введённый размер фиксирует курсор и станет размером эскиза. Пустое поле — размер задаётся мышью.")
                      : Tr("Укажите три точки в поле эскиза."),
                inner);
    ImGui::EndGroup();
}

void SketchEditor::DrawPicks(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const doc::SketchFeature* f = Active(session);
    if (!f) return;
    const auto kindName = [](sketch::EntityKind k) {
        switch (k) {
            case sketch::EntityKind::Point:  return Tr("Точка");
            case sketch::EntityKind::Line:   return Tr("Отрезок");
            case sketch::EntityKind::Circle: return Tr("Окружность");
            case sketch::EntityKind::Arc:    return Tr("Дуга");
            default:                         return Tr("Элемент");
        }
    };
    ui::FieldLabel(ui, Tr("Выбранные элементы"));
    ImGui::Dummy({0, ui.S(4)});
    std::optional<std::size_t> drop;
    const float h = ui.S(34);
    for (std::size_t i = 0; i <= myPicks.size(); ++i) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool filled = i < myPicks.size();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(filled ? c.accentSoft : c.panel), ui.S(8));
        dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(filled ? c.accentSoftBorder : c.divider), ui.S(8));
        const std::string text = filled ? std::to_string(i + 1) + ". " + kindName(f->sketch.KindOf(myPicks[i]))
                                        : Trf("{}. щёлкните элемент в эскизе", i + 1);
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(12), p.y + (h - ui.S(13)) / 2 - ui.S(1)},
                    ui::ToU32(filled ? c.text : c.textMuted), text.c_str());
        if (filled) {
            ImGui::SetCursorScreenPos({p.x + width - ui.S(32), p.y + ui.S(4)});
            ImGui::PushID(static_cast<int>(i));
            if (ui::IconButton(ui, "##drop-pick", "ui-close", 26, 12, Tr("Убрать и выбрать другой"))) drop = i;
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos({p.x, p.y + h + ui.S(6)});
        if (!filled) break;
    }
    if (drop) myPicks.erase(myPicks.begin() + static_cast<std::ptrdiff_t>(*drop));
    ImGui::Dummy({0, ui.S(8)});
    Caption(ui, Tr(InfoOf(myTool).hint), width);
    ImGui::Dummy({0, ui.S(4)});
    Caption(ui, Tr("Ошиблись — щёлкните элемент ещё раз или нажмите крестик и выберите другой."), width);
}

} // namespace bz::app
