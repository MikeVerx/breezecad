// The only two windows on top of the work: "save changes?" and "about".
// Both close with Esc.

#include "app/shell/Shell.h"
#include "ui/Widgets.h"

namespace bz::app {
namespace {

bool BeginDialog(ui::Ui& ui, const char* id, bool& open, float width) {
    if (open && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({width, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(24, 22));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ui.S(8, 8));
    const bool visible = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    if (!visible) open = false;
    return visible;
}

} // namespace

void DrawDialogs(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    // --- Save changes? ---
    bool unsavedOpen = app.shell.closeRequest.has_value();
    if (BeginDialog(ui, "##unsaved", unsavedOpen, ui.S(460))) {
        const int index = app.shell.closeRequest.value_or(-1);
        Session* s = index >= 0 && index < static_cast<int>(app.workspace.sessions.size())
                         ? app.workspace.sessions[static_cast<std::size_t>(index)].get()
                         : nullptr;
        const auto finish = [&](bool keepQuitting) {
            app.shell.closeRequest.reset();
            if (!keepQuitting) app.shell.quitRequested = false;
            ImGui::CloseCurrentPopup();
        };
        if (!s) {
            finish(true);
        } else {
            {
                ui::FontScope font(ui.fonts.title);
                ImGui::TextUnformatted("Сохранить изменения?");
            }
            {
                ui::FontScope font(ui.fonts.body);
                ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textSecondary));
                ImGui::PushTextWrapPos(0);
                ImGui::Text("В документе «%s» есть несохранённые изменения. Если не сохранить, они пропадут.", s->Title().c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0, ui.S(8)});
            const float bw = (ImGui::GetContentRegionAvail().x - ui.S(16)) / 3;
            if (ui::PrimaryButton(ui, "##save", "Сохранить", nullptr, {bw, ui.S(36)})) {
                if (app.workspace.Save(*s, false, app.notes, app.settings)) {
                    app.autosave.Discard(*s);
                    app.workspace.Close(index);
                    finish(true);
                } else {
                    finish(false);
                }
            }
            ImGui::SameLine(0, ui.S(8));
            if (ui::SecondaryButton(ui, "##discard", "Не сохранять", nullptr, {bw, ui.S(36)})) {
                app.autosave.Discard(*s);
                app.workspace.Close(index);
                finish(true);
            }
            ImGui::SameLine(0, ui.S(8));
            if (ui::SecondaryButton(ui, "##cancel", "Отмена", "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                finish(false);
        }
        ImGui::EndPopup();
    }

    // --- About ---
    if (BeginDialog(ui, "##about", app.shell.showAbout, ui.S(480))) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ui.icons.Draw(dl, "brand-mark", p, ui.S(48), {1, 1, 1, 1});
        dl->AddText(ui.fonts.display, ui.S(24), {p.x + ui.S(62), p.y + ui.S(2)}, ui::ToU32(c.text), "BreezeCAD 0.2.0");
        dl->AddText(ui.fonts.body, ui.S(13), {p.x + ui.S(63), p.y + ui.S(32)}, ui::ToU32(c.textMuted),
                    "Свободный параметрический САПР");
        ImGui::Dummy({0, ui.S(60)});
        {
            ui::FontScope font(ui.fonts.bodySmall);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textSecondary));
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted("Работает полностью офлайн. Никакие данные никуда не отправляются.");
            ImGui::TextUnformatted("Лицензия MPL-2.0. Файлы .bzc — открытый формат (ZIP + JSON).");
            ImGui::Dummy({0, ui.S(4)});
            ImGui::TextUnformatted("Использует: Open CASCADE Technology (LGPL 2.1), Dear ImGui, GLFW, lunasvg, "
                                   "nlohmann/json, miniz, portable-file-dialogs (MIT/zlib), шрифты IBM Plex (OFL).");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::Dummy({0, ui.S(8)});
        if (ui::SecondaryButton(ui, "##about-close", "Закрыть", "Esc", {ui.S(140), ui.S(34)}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.shell.showAbout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace bz::app
