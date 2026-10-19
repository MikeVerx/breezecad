// Left rail (as in KOMPAS) and the panels it switches: part properties and
// the variables table. The tree is in TreePanel.cpp.

#include "app/Format.h"
#include "app/assembly/AssemblyUi.h"
#include "app/features/OperationEditors.h"
#include "app/shell/Shell.h"
#include "app/viewport/Measure.h"
#include "app/sketch/SketchEditor.h"
#include "core/Expression.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace bz::app {
namespace {

/// Panel title row: 40 px with a hairline below.
void PanelHeader(ui::Ui& ui, const char* title, float width) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(40);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ui.fonts.strong, ui.S(13), {p.x + ui.S(14), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(ui.C().text), title);
    dl->AddLine({p.x, p.y + h - 1}, {p.x + width, p.y + h - 1}, ui::ToU32(ui.C().dividerSoft));
    ImGui::Dummy({width, h});
}

/// One property row: caption above an input that commits as an undo step.
void PropertyField(AppContext& app, Session& session, const char* label, const char* id, std::string doc::PartInfo::*field,
                   float width, const char* hint) {
    ui::Ui& ui = app.ui;
    ui::FieldLabel(ui, label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRect(p, {p.x + width, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
    if (ui::CommitInput(ui, id, session.doc.info.*field, app.shell.edit, width, false, hint)) {
        doc::PartInfo info = session.doc.info;
        info.*field = app.shell.edit.text;
        session.Execute(doc::SetPartInfo(std::move(info)));
    }
    ImGui::Dummy({0, ui.S(10)});
}

} // namespace

void DrawPropertiesPanel(AppContext& app, Session& session, float width) {
    if (session.assembly) {
        DrawAssemblyProperties(app, session, width);
        return;
    }
    ui::Ui& ui = app.ui;
    PanelHeader(ui, Tr("Свойства детали"), width);
    const float inner = width - ui.S(28);
    ImGui::SetCursorPos({ui.S(14), ImGui::GetCursorPosY() + ui.S(14)});
    ImGui::BeginGroup();
    PropertyField(app, session, Tr("Наименование"), "##name", &doc::PartInfo::name, inner, session.Title().c_str());
    PropertyField(app, session, Tr("Обозначение"), "##designation", &doc::PartInfo::designation, inner, Tr("[ОБОЗНАЧЕНИЕ]"));
    PropertyField(app, session, Tr("Материал"), "##material", &doc::PartInfo::material, inner, Tr("Не задан"));

    ui::FieldLabel(ui, Tr("Единицы длины"));
    static constexpr std::array<const char*, 4> lengths{N_("мм"), N_("см"), N_("м"), N_("дюйм")};
    const int length = static_cast<int>(session.doc.units.length);
    const int newLength = ui::Segmented(ui, "units", lengths, length, 4, inner);
    ImGui::Dummy({0, ui.S(10)});
    ui::FieldLabel(ui, Tr("Углы"));
    static constexpr std::array<const char*, 2> angles{N_("градусы"), N_("радианы")};
    const int angle = static_cast<int>(session.doc.units.angle);
    const int newAngle = ui::Segmented(ui, "angles", angles, angle, 2, inner);
    if (newLength != length || newAngle != angle) {
        UnitSystem units = session.doc.units;
        units.length = static_cast<LengthUnit>(newLength);
        units.angle = static_cast<AngleUnit>(newAngle);
        session.Execute(doc::SetUnits(units));
    }
    ImGui::Dummy({0, ui.S(16)});

    // Summary of the part.
    {
        ui::FontScope font(ui.fonts.bodySmall);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textSecondary));
        ImGui::TextUnformatted(Trf("Операций: {}", session.doc.FeatureCount()).c_str());
        ImGui::TextUnformatted(Trf("Тел: {}", session.model.Bodies().size()).c_str());
        const double volume = session.model.Volume();
        const double scale = MillimetersPer(session.doc.units.length);
        const std::string v = FormatNumber(volume / (scale * scale * scale), 2) + " " + UnitLabel(Quantity::Length, session.doc.units) + "\xC2\xB3";
        ImGui::TextUnformatted(Trf("Объём: {}", v).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
}

void DrawVariablesPanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        PanelHeader(ui, Tr("Переменные"), width);
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({p.x + width - ui.S(108), p.y + ui.S(7)});
        if (ui::LinkButton(ui, "##add-var", Tr("+ Добавить"))) {
            std::vector<Parameter> vars = session.doc.variables;
            int n = 1;
            const auto taken = [&](const std::string& name) {
                for (const Parameter* q : session.doc.AllParameters())
                    if (q->name == name) return true;
                return false;
            };
            while (taken("v" + std::to_string(n))) ++n;
            vars.push_back({"v" + std::to_string(n), "10", Quantity::Length, 10.0, {}});
            session.Execute(doc::SetVariables(std::move(vars)));
        }
        ImGui::SetCursorScreenPos(after);
    }

    const float x0 = ImGui::GetCursorScreenPos().x + ui.S(8);
    const float inner = width - ui.S(16);
    const float colName = inner * 0.30f, colExpr = inner * 0.42f;   // the value takes the rest
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto headerRow = [&](const char* a, const char* b, const char* cText) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(26);
        dl->AddText(ui.fonts.caption, ui.S(11), {x0 + ui.S(6), p.y + ui.S(7)}, ui::ToU32(c.textMuted), a);
        dl->AddText(ui.fonts.caption, ui.S(11), {x0 + colName + ui.S(6), p.y + ui.S(7)}, ui::ToU32(c.textMuted), b);
        const float w = ui.fonts.caption->CalcTextSizeA(ui.S(11), FLT_MAX, 0, cText).x;
        dl->AddText(ui.fonts.caption, ui.S(11), {x0 + inner - w - ui.S(6), p.y + ui.S(7)}, ui::ToU32(c.textMuted), cText);
        dl->AddLine({x0, p.y + h}, {x0 + inner, p.y + h}, ui::ToU32(c.dividerSoft));
        ImGui::Dummy({inner, h + ui.S(2)});
    };

    /// One row; returns the committed new expression, if any.
    const auto row = [&](const Parameter& p, const char* id, bool nameEditable, std::string* newName) -> std::optional<std::string> {
        ImGui::PushID(id);
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        std::optional<std::string> result;
        ImGui::SetCursorScreenPos({x0, rp.y});
        if (nameEditable && newName) {
            if (ui::CommitInput(ui, "##name", p.name, app.shell.edit, colName - ui.S(4), true)) *newName = app.shell.edit.text;
        } else {
            dl->AddText(ui.fonts.mono, ui.S(12), {x0 + ui.S(8), rp.y + ui.S(7)}, ui::ToU32(c.text), p.name.c_str());
        }
        ImGui::SetCursorScreenPos({x0 + colName, rp.y});
        if (ui::CommitInput(ui, "##expr", p.expression, app.shell.edit, colExpr - ui.S(4), true)) result = app.shell.edit.text;
        const std::string value = p.HasError() ? Tr("ошибка") : FormatValue(p.value, p.quantity, session.doc.units, 3) + " " +
                                                              UnitLabel(p.quantity, session.doc.units);
        const float vw = ui.fonts.mono->CalcTextSizeA(ui.S(12), FLT_MAX, 0, value.c_str()).x;
        dl->AddText(ui.fonts.mono, ui.S(12), {x0 + inner - vw - ui.S(6), rp.y + ui.S(7)},
                    ui::ToU32(p.HasError() ? c.danger : c.textSecondary), value.c_str());
        ImGui::SetCursorScreenPos({x0, rp.y});
        ImGui::Dummy({inner, ui.S(28)});
        if (p.HasError()) ui::ItemTooltip(ui, Tr(p.error).c_str());
        dl->AddLine({x0, rp.y + ui.S(29)}, {x0 + inner, rp.y + ui.S(29)}, ui::ToU32(c.dividerSoft));
        ImGui::Dummy({0, ui.S(2)});
        ImGui::PopID();
        return result;
    };

    ImGui::Dummy({0, ui.S(6)});
    headerRow(Tr("Имя"), Tr("Выражение"), Tr("Значение"));
    if (session.doc.variables.empty()) {
        ui::FontScope font(ui.fonts.bodySmall);
        ImGui::SetCursorScreenPos({x0 + ui.S(6), ImGui::GetCursorScreenPos().y + ui.S(4)});
        ImGui::PushTextWrapPos(x0 + inner - ImGui::GetWindowPos().x);
        ImGui::TextColored(ui::ToVec4(c.textMuted), "%s", Tr("Переменных пока нет. Добавьте, например, ширину детали и используйте её в размерах: w / 2."));
        ImGui::PopTextWrapPos();
    }
    for (std::size_t i = 0; i < session.doc.variables.size(); ++i) {
        const Parameter& p = session.doc.variables[i];
        std::string newName;
        const std::string id = "var" + std::to_string(i);
        const auto expr = row(p, id.c_str(), true, &newName);
        bool erase = false;
        {
            ui::MenuStyle style(app.ui);   // before Begin: must outlive the End call
            if (ImGui::BeginPopupContextItem(("##ctx" + id).c_str())) {
                erase = ui::MenuItem(app.ui, Tr("Удалить переменную"), nullptr, "ui-trash");
                ImGui::EndPopup();
            }
        }
        if (erase) {
            // A variable still used would leave its users with "unknown name":
            // say where it is used instead of breaking them silently.
            std::vector<std::string> users;
            for (const Parameter* q : std::as_const(session.doc).AllParameters()) {
                if (q == &p) continue;
                const std::vector<std::string> names = ReferencedNames(q->expression);
                if (std::ranges::find(names, p.name) != names.end()) users.push_back(q->name.empty() ? q->expression : q->name);
            }
            if (!users.empty()) {
                std::string list;
                for (std::size_t k = 0; k < users.size() && k < 5; ++k) list += (k ? ", " : "") + users[k];
                if (users.size() > 5) list += ", …";
                app.notes.Error(Trf("«{}» используется: {}. Сначала уберите её из этих выражений", p.name, list));
                break;
            }
            std::vector<Parameter> vars = session.doc.variables;
            vars.erase(vars.begin() + static_cast<std::ptrdiff_t>(i));
            session.Execute(doc::SetVariables(std::move(vars)));
            break;
        }
        if (!newName.empty() && newName != p.name) {
            if (!IsValidVariableName(newName)) {
                app.notes.Error(Trf("Имя «{}» не подходит: буквы, цифры и «_», начинается с буквы", newName));
                break;
            }
            const auto taken = std::as_const(session.doc).AllParameters();
            if (std::ranges::any_of(taken, [&](const Parameter* q) { return q->name == newName; })) {
                app.notes.Error(Trf("Имя «{}» уже занято", newName));
                break;
            }
            // The expressions that use the variable follow its new name.
            session.Execute(doc::RenameVariable(i, newName));
            break;   // the table changed; draw the rest next frame
        }
        if (expr) {
            std::vector<Parameter> vars = session.doc.variables;
            vars[i].expression = *expr;
            session.Execute(doc::SetVariables(std::move(vars)));
            break;   // the table changed; draw the rest next frame
        }
    }

    // Dimensions and operation values, editable here too (like KOMPAS).
    ImGui::Dummy({0, ui.S(14)});
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ui.fonts.strong, ui.S(12), {x0 + ui.S(6), p.y}, ui::ToU32(c.text), Tr("Размеры модели"));
        ImGui::Dummy({inner, ui.S(18)});
    }
    headerRow(Tr("Имя"), Tr("Выражение"), Tr("Значение"));
    std::unique_ptr<doc::Feature> edited;   // applied after the loop: never edit while iterating
    std::string                   label;
    for (const auto& feature : session.doc.Features()) {
        for (const Parameter* p : feature->Parameters()) {
            if (p->name.empty()) continue;
            const std::string id = "p" + std::to_string(feature->id.value) + p->name;
            const auto expr = row(*p, id.c_str(), false, nullptr);
            ui::ItemTooltip(ui, feature->name.c_str());
            if (!expr || edited) continue;
            edited = feature->Clone();
            std::vector<Parameter*> params;
            edited->CollectParameters(params);
            for (Parameter* q : params)
                if (q->name == p->name) q->expression = *expr;
            label = feature->name + ": " + p->name;
        }
    }
    if (edited) session.Execute(doc::ReplaceFeature(std::move(edited), label));
}

void DrawLeftArea(AppContext& app, Session& session, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float railW = ui.S(ui.theme.metrics.railWidth);

    // Rail with the three panel tabs.
    {
        ImDrawList* dl = BeginArea("##rail", pos, {railW, size.y}, c.appBackground);
        dl->AddLine({pos.x + railW - 1, pos.y}, {pos.x + railW - 1, pos.y + size.y}, ui::ToU32(c.divider));
        const struct {
            const char* id;
            const char* icon;
            const char* tip;
            LeftTab     tab;
        } tabs[] = {{"##rail-tree", "ui-tree", session.assembly ? Tr("Дерево сборки") : Tr("Дерево построения"), LeftTab::Tree},
                    {"##rail-props", "ui-sliders", session.assembly ? Tr("Свойства") : Tr("Свойства детали"), LeftTab::Properties},
                    {"##rail-vars", "ui-variables", Tr("Переменные"), LeftTab::Variables}};
        float y = pos.y + ui.S(8);
        if (session.assembly && app.shell.leftTab == LeftTab::Variables) app.shell.leftTab = LeftTab::Tree;
        for (const auto& t : tabs) {
            if (session.assembly && t.tab == LeftTab::Variables) continue;   // an assembly has no variables yet
            ImGui::SetCursorScreenPos({pos.x + (railW - ui.S(36)) / 2, y});
            const bool active = app.shell.leftTab == t.tab && !app.ops.Active();
            if (ui::IconButton(ui, t.id, t.icon, 36, 20, t.tip, true, active)) {
                if (app.ops.Active()) app.Later([&app, &session] { app.ops.Commit(app, session); });
                app.shell.measuring = false;   // its panel gives way to the one asked for
                app.shell.leftTab = t.tab;
            }
            y += ui.S(40);
        }
        EndArea();
    }

    // The panel itself.
    const ImVec2 panelPos{pos.x + railW, pos.y};
    const ImVec2 panelSize{size.x - railW, size.y};
    ImDrawList* dl = BeginArea("##leftpanel", panelPos, panelSize, c.panel, true);
    dl->AddLine({panelPos.x + panelSize.x - 1, panelPos.y}, {panelPos.x + panelSize.x - 1, panelPos.y + panelSize.y},
                ui::ToU32(c.divider));
    const float width = panelSize.x - 1;
    if (OperationEditor* operation = app.ops.Active()) {
        operation->DrawPanel(app, session);
    } else if (app.shell.measuring && session.mode == EditMode::Model) {
        DrawMeasurePanel(app, session, width);
    } else if (session.activeSketch && app.sketch.HasToolPanel()) {
        app.sketch.DrawToolPanel(app, session, width);
    } else {
        switch (app.shell.leftTab) {
            case LeftTab::Tree:       DrawTreePanel(app, session, width); break;
            case LeftTab::Properties: DrawPropertiesPanel(app, session, width); break;
            case LeftTab::Variables:  DrawVariablesPanel(app, session, width); break;
        }
    }
    EndArea();
}

} // namespace bz::app
