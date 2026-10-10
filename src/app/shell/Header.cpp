// Header row: brand menu, quick access toolbar, document title, command
// search with its drop-down, and live stats (FPS, memory, objects).

#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <imgui_stdlib.h>

#include <format>

namespace bz::app {

ImDrawList* BeginArea(const char* id, ImVec2 pos, ImVec2 size, const Color& background, bool scroll) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::ToVec4(background));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (!scroll) flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild(id, size, ImGuiChildFlags_None, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return ImGui::GetWindowDrawList();
}

void EndArea() { ImGui::EndChild(); }

namespace {

void MenuAction(AppContext& app, const char* id) {
    const Action* a = app.actions.Find(id);
    if (!a) return;
    const std::string shortcut = ActionRegistry::ChordText(a->chord);
    if (ui::MenuItem(app.ui, a->Label(), shortcut.empty() ? nullptr : shortcut.c_str(), a->icon, false, a->IsEnabled())) {
        const std::string action = a->id;
        app.Later([&app, action] { app.actions.Run(action); });
    }
}

void MainMenu(AppContext& app) {
    ui::Ui& ui = app.ui;
    ui::MenuStyle style(ui);   // before Begin: must outlive the End call
    if (!ImGui::BeginPopup("##main-menu")) return;
    MenuAction(app, "file.new");
    MenuAction(app, "file.open");
    if (ui::BeginSubmenu(ui, Tr("Последние"), "ui-file", !app.settings.recentFiles.empty())) {
        for (const auto& file : app.settings.recentFiles) {
            const std::u8string name = file.filename().u8string();
            if (ui::MenuItem(ui, reinterpret_cast<const char*>(name.c_str()), nullptr, "ui-file")) {
                const std::filesystem::path chosen = file;
                app.Later([&app, chosen] { OpenDocument(app, chosen); });
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    MenuAction(app, "file.save");
    MenuAction(app, "file.saveAs");
    if (ui::BeginSubmenu(ui, Tr("Экспорт"), "ui-export", app.Doc() != nullptr)) {
        MenuAction(app, "file.exportStep");
        MenuAction(app, "file.exportStl");
        MenuAction(app, "file.export3mf");
        ImGui::EndMenu();
    }
    ImGui::Separator();
    MenuAction(app, "app.settings");
    if (ui::BeginSubmenu(ui, Tr("Оформление"), "theme")) {
        if (ui::MenuItem(ui, Tr("Как в системе"), nullptr, {}, app.settings.theme == ThemeChoice::System)) SetTheme(app, ThemeChoice::System);
        if (ui::MenuItem(ui, Tr("Светлая"), nullptr, {}, app.settings.theme == ThemeChoice::Light)) SetTheme(app, ThemeChoice::Light);
        if (ui::MenuItem(ui, Tr("Тёмная"), nullptr, {}, app.settings.theme == ThemeChoice::Dark)) SetTheme(app, ThemeChoice::Dark);
        ImGui::Separator();
        if (ui::MenuItem(ui, Tr("Анимации камеры"), nullptr, {}, app.settings.animations)) app.settings.animations = !app.settings.animations;
        ImGui::EndMenu();
    }
    if (ui::BeginSubmenu(ui, Tr("Автосохранение"), "ui-save")) {
        for (int minutes : {0, 1, 5, 10, 15}) {
            const std::string label = minutes == 0 ? Tr("Выключено") : Trf("Каждые {} мин", minutes);
            if (ui::MenuItem(ui, label.c_str(), nullptr, {}, app.settings.autosaveMinutes == minutes))
                app.settings.autosaveMinutes = minutes;
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    MenuAction(app, "file.close");
    MenuAction(app, "app.about");
    MenuAction(app, "app.quit");
    ImGui::EndPopup();
}

void DisplayStyleMenu(AppContext& app) {
    ui::Ui& ui = app.ui;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(6, 6));
    ui::FontScope font(ui.fonts.body);   // before Begin: must outlive the End call
    if (ImGui::BeginPopup("##display-style")) {
        for (const char* id : {"view.shaded", "view.edges", "view.wireframe", "view.hidden", "view.transparent"}) {
            const Action* a = app.actions.Find(id);
            ImGui::PushID(id);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##style", false, 0, {ui.S(200), ui.S(28)})) a->run();
            ui.icons.Draw(ImGui::GetWindowDrawList(), a->icon, {p.x + ui.S(4), p.y + ui.S(4)}, ui.S(20), {1, 1, 1, 1});
            ImGui::GetWindowDrawList()->AddText({p.x + ui.S(32), p.y + ui.S(6)}, ui::ToU32(ui.C().text), a->Label());
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

/// Search field and the drop-down with matching commands (Ctrl+K).
void CommandSearch(AppContext& app, ImVec2 pos, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ui.S(28);
    const ImVec2 p{pos.x, pos.y};
    const bool open = app.shell.paletteOpen;
    dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.field), ui.S(8));
    dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(open ? c.accent : c.fieldBorder), ui.S(8));
    ui.icons.Draw(dl, "ui-search", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.textMuted);

    ImGui::SetCursorScreenPos({p.x + ui.S(30), p.y});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, (h - ui.S(12)) / 2});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0, 0, 0, 0});
    ImGui::PushFont(ui.fonts.bodySmall);
    if (app.shell.focusPalette) {
        ImGui::SetKeyboardFocusHere();
        app.shell.focusPalette = false;
    }
    ImGui::SetNextItemWidth(width - ui.S(80));
    ImGui::InputTextWithHint("##search", Tr("Поиск команд"), &app.shell.paletteQuery);
    if (ImGui::IsItemActivated()) app.shell.paletteOpen = true;
    const bool fieldActive = ImGui::IsItemActive();
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    const char* chip = "Ctrl K";
    const ImVec2 cs = ui.fonts.mono->CalcTextSizeA(ui.S(11), FLT_MAX, 0, chip);
    const ImVec2 cp{p.x + width - cs.x - ui.S(14), p.y + (h - cs.y) / 2};
    dl->AddRect({cp.x - ui.S(4), cp.y - ui.S(1)}, {cp.x + cs.x + ui.S(4), cp.y + cs.y + ui.S(1)}, ui::ToU32(c.fieldBorder), ui.S(4));
    dl->AddText(ui.fonts.mono, ui.S(11), cp, ui::ToU32(c.textMuted), chip);

    if (!app.shell.paletteOpen) return;
    const std::vector<const Action*> found = app.actions.Search(app.shell.paletteQuery);
    ImGui::SetNextWindowPos({p.x, p.y + h + ui.S(4)});
    ImGui::SetNextWindowSize({width + ui.S(120), 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ui::FontScope font(ui.fonts.body);   // before Begin: must outlive the End call
    ImGui::Begin("##palette", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
    int shown = 0;
    bool ran = false;
    for (const Action* a : found) {
        if (shown >= 9) break;
        ++shown;
        ImGui::PushID(a->id.c_str());
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        const bool enabled = a->IsEnabled();
        const bool first = shown == 1;
        const bool chosen = ImGui::Selectable("##cmd", first && !app.shell.paletteQuery.empty(),
                                              enabled ? 0 : ImGuiSelectableFlags_Disabled, {width + ui.S(108), ui.S(28)});
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        if (!a->icon.empty()) ui.icons.Draw(pdl, a->icon, {rp.x + ui.S(4), rp.y + ui.S(5)}, ui.S(18), c.textSecondary);
        pdl->AddText({rp.x + ui.S(30), rp.y + ui.S(6)}, ui::ToU32(enabled ? c.text : c.textDisabled), a->Label());
        const std::string sc = ActionRegistry::ChordText(a->chord);
        if (!sc.empty()) {
            const ImVec2 ss = ui.fonts.mono->CalcTextSizeA(ui.S(11), FLT_MAX, 0, sc.c_str());
            pdl->AddText(ui.fonts.mono, ui.S(11), {rp.x + width + ui.S(100) - ss.x, rp.y + ui.S(8)}, ui::ToU32(c.textMuted), sc.c_str());
        }
        const bool enter = first && fieldActive && ImGui::IsKeyPressed(ImGuiKey_Enter);
        if ((chosen || enter) && enabled && !ran) {
            ran = true;
            const std::string id = a->id;
            app.Later([&app, id] { app.actions.Run(id); });
        }
        ImGui::PopID();
    }
    if (shown == 0) ImGui::TextDisabled("%s", Tr("Ничего не найдено"));
    const bool hovered = ImGui::IsWindowHovered();
    ImGui::End();
    ImGui::PopStyleVar(3);

    if (ran || (!fieldActive && !hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
        app.shell.paletteOpen = false;
        app.shell.paletteQuery.clear();
    }
}

} // namespace

void DrawHeader(AppContext& app, ImVec2 pos, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float h = ui.S(app.ui.theme.metrics.headerHeight);
    ImDrawList* dl = BeginArea("##header", pos, {width, h}, c.appBackground);
    dl->AddLine({pos.x, pos.y + h - 1}, {pos.x + width, pos.y + h - 1}, ui::ToU32(c.divider));

    const float cy = pos.y + (h - ui.S(30)) / 2;
    // Brand menu: the B-flow mark opens the main menu.
    ImGui::SetCursorScreenPos({pos.x + ui.S(10), cy});
    const ImVec2 mp = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##brand", ui.S(46, 30))) ImGui::OpenPopup("##main-menu");
    if (ImGui::IsItemHovered()) dl->AddRectFilled(mp, {mp.x + ui.S(46), mp.y + ui.S(30)}, ui::ToU32(c.hover), ui.S(6));
    ui.icons.Draw(dl, "brand-mark", {mp.x + ui.S(6), mp.y + ui.S(4)}, ui.S(22), {1, 1, 1, 1});
    ui.icons.Draw(dl, "ui-chevron-down", {mp.x + ui.S(30), mp.y + ui.S(10)}, ui.S(10), c.textMuted);
    ui::ItemTooltip(ui, Tr("Меню BreezeCAD"));
    MainMenu(app);

    float x = mp.x + ui.S(54);
    dl->AddLine({x, pos.y + ui.S(10)}, {x, pos.y + h - ui.S(10)}, ui::ToU32(c.fieldBorder));
    x += ui.S(8);

    // Quick access toolbar.
    const auto qat = [&](const char* actionId) {
        const Action* a = app.actions.Find(actionId);
        ImGui::SetCursorScreenPos({x, cy});
        std::string tip = a->Label();
        if (const std::string sc = ActionRegistry::ChordText(a->chord); !sc.empty()) tip += " (" + sc + ")";
        if (std::string undo = actionId == std::string("edit.undo") && app.Doc() ? Tr(app.Doc()->UndoLabel()) : "";
            !undo.empty())
            tip += ": " + undo;
        if (ui::IconButton(ui, actionId, a->icon, 30, 18, tip.c_str(), a->IsEnabled())) {
            const std::string id = actionId;
            app.Later([&app, id] { app.actions.Run(id); });
        }
        x += ui.S(32);
    };
    const auto separator = [&] {
        dl->AddLine({x + ui.S(3), pos.y + ui.S(11)}, {x + ui.S(3), pos.y + h - ui.S(11)}, ui::ToU32(c.fieldBorder));
        x += ui.S(8);
    };
    qat("file.new");
    qat("file.open");
    qat("file.save");
    separator();
    qat("edit.undo");
    qat("edit.redo");
    separator();
    qat("app.home");
    {
        ImGui::SetCursorScreenPos({x, cy});
        if (ui::IconButton(ui, "##qat-style", "ui-display-style", 30, 18, Tr("Стиль отображения"), app.Doc() != nullptr))
            ImGui::OpenPopup("##display-style");
        DisplayStyleMenu(app);
        x += ui.S(32);
    }
    // The toolbar's own icons for two ribbon commands.
    const auto qatAs = [&](const char* id, const char* actionId, const char* icon) {
        const Action* a = app.actions.Find(actionId);
        ImGui::SetCursorScreenPos({x, cy});
        if (ui::IconButton(ui, id, icon, 30, 18, a->Label(), a->IsEnabled())) {
            const std::string action = actionId;
            app.Later([&app, action] { app.actions.Run(action); });
        }
        x += ui.S(32);
    };
    qatAs("##qat-materials", "view.appearance", "ui-materials");
    ImGui::SetCursorScreenPos({x, cy});
    if (ui::IconButton(ui, "##qat-fx", "ui-fx", 30, 18, Tr("Переменные (fx)"), app.Doc() != nullptr))
        app.shell.leftTab = LeftTab::Variables;
    x += ui.S(32);
    qatAs("##qat-measure", "tools.measure", "ui-ruler");
    // The part's appearance, as in Inventor's toolbar.
    x += ui.S(6);
    app.ops.appearance.DrawToolbarPicker(app, app.workspace.ShowsHome() ? nullptr : app.Doc(), {x, cy}, ui.S(170), ui.S(30));
    x += ui.S(176);

    // Right side: search and stats.
    const float statsWidth = ui.S(190);
    const float searchWidth = ui.S(240);
    const float right = pos.x + width - ui.S(10);
    {
        ui::FontScope font(ui.fonts.mono);
        Session* s = app.Doc();
        const int objects = s ? static_cast<int>(s->doc.FeatureCount()) : 0;
        // Frames are drawn only when something changes, so frames per second
        // would count mouse events; the work per frame is the honest figure.
        const double workMs = app.stats.uiMs + app.stats.sceneMs;
        const std::string fps = std::format("{:.1f}", workMs);
        const std::string mem = std::format("{:.0f}", app.stats.memoryMb);
        const std::string obj = std::to_string(objects);
        float sx = right - statsWidth + ui.S(12);
        const float sy = pos.y + (h - ui.S(12)) / 2;
        const auto stat = [&](const std::string& value, const char* unit, const Color& color) {
            dl->AddText(ui.fonts.mono, ui.S(11), {sx, sy}, ui::ToU32(color), value.c_str());
            sx += ui.fonts.mono->CalcTextSizeA(ui.S(11), FLT_MAX, 0, value.c_str()).x + ui.S(4);
            dl->AddText(ui.fonts.mono, ui.S(11), {sx, sy}, ui::ToU32(c.textMuted), unit);
            sx += ui.fonts.mono->CalcTextSizeA(ui.S(11), FLT_MAX, 0, unit).x + ui.S(12);
        };
        const float statsLeft = sx;
        stat(fps, Tr("мс"), workMs < 8 ? c.success : workMs < 25 ? c.warning : c.danger);
        stat(mem, Tr("МБ"), c.text);
        stat(obj, "OBJ", c.text);
        // Where a frame's time goes, for performance reports.
        if (ImGui::IsMouseHoveringRect({statsLeft, pos.y}, {right, pos.y + h})) {
            const FrameStats& st = app.stats;
            const std::string tip = Trf("Работа над кадром: {:.1f} мс интерфейс (из них {:.2f} мс — поиск под курсором),\n"
                                        "{:.1f} мс модель; вывод на экран {:.1f} мс (включает ожидание монитора)\n"
                                        "Кадров за секунду: {:.0f}, перерисовок модели: {}\n"
                                        "Кадр рисуется только когда что-то меняется.\n"
                                        "Память — занятая программой (как «Память» в диспетчере задач),\n"
                                        "обновляется раз в секунду.",
                                        st.uiMs, st.hoverMs, st.sceneMs, st.presentMs, st.fps, st.sceneFrames);
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    const float searchX = right - statsWidth - searchWidth;

    // Title in the middle of what is left.
    {
        Session* s = app.Doc();
        const std::string name = s && !app.workspace.ShowsHome() ? s->Title() : Tr("Главная");
        const char* suffix = " \xE2\x80\x94 BreezeCAD";
        ImFont* f1 = ui.fonts.medium;
        ImFont* f2 = ui.fonts.body;
        const float w1 = f1->CalcTextSizeA(ui.S(13), FLT_MAX, 0, name.c_str()).x;
        const float w2 = f2->CalcTextSizeA(ui.S(13), FLT_MAX, 0, suffix).x;
        const float mid = (x + searchX) / 2;
        float tx = std::max(x + ui.S(8), mid - (w1 + w2) / 2);
        if (tx + w1 + w2 < searchX - ui.S(8)) {
            const float ty = pos.y + (h - ui.S(13)) / 2 - ui.S(1);
            dl->AddText(f1, ui.S(13), {tx, ty}, ui::ToU32(c.text), name.c_str());
            dl->AddText(f2, ui.S(13), {tx + w1, ty}, ui::ToU32(c.textMuted), suffix);
            if (s && s->IsModified() && !app.workspace.ShowsHome())
                dl->AddCircleFilled({tx + w1 + w2 + ui.S(8), ty + ui.S(8)}, ui.S(3), ui::ToU32(c.accent));
        }
    }

    CommandSearch(app, {searchX, pos.y + (h - ui.S(28)) / 2}, searchWidth);
    EndArea();
}

} // namespace bz::app
