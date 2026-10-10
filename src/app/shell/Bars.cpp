// Bottom bars: open documents as tabs and the information line.

#include "app/Format.h"
#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "app/shell/Shell.h"
#include "app/sketch/SketchEditor.h"
#include "core/I18n.h"
#include "kernel/Model.h"
#include "document/SketchFeature.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <format>

namespace bz::app {

void DrawProjectTabs(AppContext& app, ImVec2 pos, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float h = ui.S(ui.theme.metrics.tabBarHeight);
    ImDrawList* dl = BeginArea("##tabs", pos, {width, h}, c.appBackground);
    dl->AddLine(pos, {pos.x + width, pos.y}, ui::ToU32(c.divider));

    float x = pos.x + ui.S(8);
    ImGui::SetCursorScreenPos({x, pos.y + (h - ui.S(30)) / 2});
    if (ui::IconButton(ui, "##tab-home", "ui-home", 32, 16, Tr("Главная страница"), true, app.workspace.ShowsHome()))
        app.workspace.ShowHome(true);
    x += ui.S(40);
    dl->AddLine({x, pos.y + ui.S(8)}, {x, pos.y + h - ui.S(8)}, ui::ToU32(c.fieldBorder));
    x += ui.S(4);

    for (std::size_t i = 0; i < app.workspace.sessions.size(); ++i) {
        Session& s = *app.workspace.sessions[i];
        const bool active = static_cast<int>(i) == app.workspace.ActiveIndex() && !app.workspace.ShowsHome();
        const std::string title = s.Title();
        ImFont* font = active ? ui.fonts.medium : ui.fonts.body;
        const float tw = font->CalcTextSizeA(ui.S(12), FLT_MAX, 0, title.c_str()).x;
        const float w = tw + ui.S(48);
        ImGui::SetCursorScreenPos({x, pos.y});
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = ImGui::InvisibleButton("##doc-tab", {w, h}, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        if (clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
            const int index = static_cast<int>(i);
            app.Later([&app, index] { RequestClose(app, index); });
        } else if (clicked) {
            const int index = static_cast<int>(i);
            app.Later([&app, index] { app.workspace.Activate(index); });
        }
        if (active) {
            dl->AddRectFilled({x, pos.y}, {x + w, pos.y + h}, ui::ToU32(c.panel));
            dl->AddRectFilled({x, pos.y}, {x + w, pos.y + ui.S(2)}, ui::ToU32(c.accent));
        } else if (hovered) {
            dl->AddRectFilled({x, pos.y}, {x + w, pos.y + h}, ui::ToU32(c.hover));
        }
        dl->AddText(font, ui.S(12), {x + ui.S(12), pos.y + (h - ui.S(12)) / 2 - ui.S(1)},
                    ui::ToU32(active ? c.text : c.textSecondary), title.c_str());
        // Unsaved dot, or a close cross on hover.
        const ImVec2 mark{x + ui.S(12) + tw + ui.S(14), pos.y + h / 2};
        if (hovered) {
            ImGui::SetCursorScreenPos({mark.x - ui.S(9), mark.y - ui.S(9)});
            if (ui::IconButton(ui, "##close-tab", "ui-close", 18, 10, Tr("Закрыть (Ctrl+W)"))) {
                const int index = static_cast<int>(i);
                app.Later([&app, index] { RequestClose(app, index); });
            }
        } else if (s.IsModified()) {
            dl->AddCircleFilled(mark, ui.S(3), ui::ToU32(c.accent));
        }
        ImGui::PopID();
        x += w + ui.S(2);
    }
    ImGui::SetCursorScreenPos({x + ui.S(2), pos.y + (h - ui.S(28)) / 2});
    if (ui::IconButton(ui, "##tab-new", "ui-plus", 28, 12, Tr("Новая деталь (Ctrl+N)"))) app.Later([&app] { NewDocument(app); });
    EndArea();
}

void DrawStatusBar(AppContext& app, ImVec2 pos, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float h = ui.S(ui.theme.metrics.statusHeight);
    ImDrawList* dl = BeginArea("##status", pos, {width, h}, c.statusBar);
    dl->AddLine(pos, {pos.x + width, pos.y}, ui::ToU32(c.divider));
    ImFont* font = ui.fonts.caption;
    const float fs = ui.S(11);
    const float ty = pos.y + (h - fs) / 2 - ui.S(1);
    float x = pos.x + ui.S(14);
    const auto text = [&](const std::string& s, const Color& color, ImFont* f = nullptr) {
        ImFont* used = f ? f : font;
        dl->AddText(used, fs, {x, ty}, ui::ToU32(color), s.c_str());
        x += used->CalcTextSizeA(fs, FLT_MAX, 0, s.c_str()).x + ui.S(18);
    };

    Session* s = app.Doc();
    if (!s || app.workspace.ShowsHome()) {
        text(Tr("Главная страница"), c.text, ui.fonts.medium);
        text(Tr("Создайте деталь (Ctrl+N) или откройте документ (Ctrl+O)"), c.textSecondary);
        EndArea();
        return;
    }

    // Mode and what to do next.
    std::string mode = Tr("Деталь"), hint;
    if (const OperationEditor* operation = app.ops.Active()) {
        mode = operation->Title();
        hint = operation->Hint();
    } else if (s->mode == EditMode::Sketch) {
        mode = Trf("Эскиз: {}", Tr(InfoOf(app.sketch.Tool()).label));
        hint = app.sketch.Hint(*s);
    } else if (s->mode == EditMode::PickPlane) {
        mode = Tr("Новый эскиз");
        hint = Tr("Выберите плоскость начала координат или плоскую грань модели. Esc — отмена");
    } else {
        const MouseNavigation& m = app.settings.mouse;
        hint = Trf("{} — вращение, {} — сдвиг, прокрутка — масштаб, двойной щелчок в дереве — редактировать",
                   GestureText(m.orbit), GestureText(m.pan));
    }
    text(mode, c.text, ui.fonts.medium);

    // Right side first (it must stay visible), then the hint takes the rest.
    float right = pos.x + width - ui.S(14);
    const auto rightText = [&](const std::string& s2, const Color& color) {
        const float w = font->CalcTextSizeA(fs, FLT_MAX, 0, s2.c_str()).x;
        right -= w;
        dl->AddText(font, fs, {right, ty}, ui::ToU32(color), s2.c_str());
        right -= ui.S(18);
    };
    // The open operation's own error is shown in its panel, not counted here.
    const OperationEditor* operation = app.ops.Active();
    const ObjectId editing = operation ? operation->EditedFeature() : ObjectId{};
    int errors = 0;
    for (const auto& f : s->doc.Features()) errors += f->status.state == doc::FeatureState::Error && f->id != editing ? 1 : 0;
    if (s->Rebuilding()) {
        // A rebuild under way in slices: a bar with how far it is.
        const kernel::Model::Progress p = s->model.RebuildProgress();
        const float barW = ui.S(90), barH = ui.S(6);
        const std::string label = Tr("Перестроение");
        const float lw = font->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str()).x;
        right -= barW;
        const float by = pos.y + (h - barH) / 2;
        dl->AddRectFilled({right, by}, {right + barW, by + barH}, ui::ToU32(c.dividerSoft), barH / 2);
        const float share = p.total > 0 ? static_cast<float>(p.done) / static_cast<float>(p.total) : 0.0f;
        dl->AddRectFilled({right, by}, {right + std::max(barH, barW * share), by + barH}, ui::ToU32(c.accent), barH / 2);
        right -= ui.S(8) + lw;
        dl->AddText(font, fs, {right, ty}, ui::ToU32(c.textSecondary), label.c_str());
        right -= ui.S(18);
    } else {
        rightText(errors ? Trf("Ошибок: {}", errors) : Tr("Готово"), errors ? c.danger : c.success);
    }
    rightText(UnitLabel(Quantity::Length, s->doc.units), c.textSecondary);
    if (s->mode == EditMode::Sketch) {
        if (const auto* sk = s->doc.FindAs<doc::SketchFeature>(s->activeSketch)) {
            const int dof = sk->analysis.degreesOfFreedom;
            if (dof == 0) rightText(Tr("Эскиз полностью определён"), c.success);
            else if (dof > 0) rightText(Trf("Степеней свободы: {}", dof), c.textSecondary);
            else rightText(Tr("Ограничения противоречат"), c.danger);
        }
        if (const auto cursor = app.sketch.Cursor()) {
            const std::string coords = "X " + FormatValue(cursor->x, Quantity::Length, s->doc.units, 2) + "   Y " +
                                       FormatValue(cursor->y, Quantity::Length, s->doc.units, 2);
            const float w = ui.fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, coords.c_str()).x;
            right -= w;
            dl->AddText(ui.fonts.mono, fs, {right, ty}, ui::ToU32(c.textSecondary), coords.c_str());
            right -= ui.S(18);
        }
        const std::string snaps = (app.settings.orthoMode ? std::string(Tr("Орто")) + "  \xC2\xB7  " : std::string()) + Trf("Привязки: {}", app.settings.pointSnap && app.settings.gridSnap ? Tr("точки, сетка")
                                                       : app.settings.pointSnap                       ? Tr("точки")
                                                       : app.settings.gridSnap                        ? Tr("сетка")
                                                                                                      : Tr("выкл"));
        const float w = font->CalcTextSizeA(fs, FLT_MAX, 0, snaps.c_str()).x;
        right -= w;
        ImGui::SetCursorScreenPos({right, pos.y});
        if (ImGui::InvisibleButton("##snaps", {w, h})) ImGui::OpenPopup("##snap-menu");
        dl->AddText(font, fs, {right, ty}, ui::ToU32(ImGui::IsItemHovered() ? c.accent : c.textSecondary), snaps.c_str());
        right -= ui.S(18);
        ui::MenuStyle style(ui);   // before Begin: must outlive the End call
        if (ImGui::BeginPopup("##snap-menu")) {
            if (ui::MenuItem(ui, Tr("Привязка к точкам и кривым"), nullptr, {}, app.settings.pointSnap))
                app.settings.pointSnap = !app.settings.pointSnap;
            if (ui::MenuItem(ui, Tr("Привязка к сетке"), nullptr, {}, app.settings.gridSnap)) app.settings.gridSnap = !app.settings.gridSnap;
            if (ui::MenuItem(ui, Tr("Ортогональный режим"), "F8", {}, app.settings.orthoMode)) app.settings.orthoMode = !app.settings.orthoMode;
            ImGui::EndPopup();
        }
    }
    const ImVec4 clip{x, pos.y, right, pos.y + h};
    dl->AddText(font, fs, {x, ty}, ui::ToU32(c.textSecondary), hint.c_str(), nullptr, 0, &clip);
    EndArea();
}

} // namespace bz::app
