// Home screen: start a part, open a file, recover unsaved work, recent files.

#include "app/Operations.h"
#include "app/shell/Shell.h"
#include "app/assembly/AssemblyUi.h"
#include "core/I18n.h"
#include "core/Version.h"
#include "core/PathText.h"
#include "ui/Widgets.h"

#include <filesystem>

namespace bz::app {
namespace {

void SectionTitle(ui::Ui& ui, const char* text) {
    ui::FontScope font(ui.fonts.strong);
    ImGui::TextUnformatted(text);
    ImGui::Dummy({0, ui.S(6)});
}

} // namespace

void DrawHomeScreen(AppContext& app, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = BeginArea("##home", pos, size, c.ribbon, true);

    const float column = std::min(size.x - ui.S(64), ui.S(720));
    const float x0 = pos.x + (size.x - column) / 2;
    float y = pos.y + std::max(ui.S(48), size.y * 0.09f);

    // Brand.
    ui.icons.Draw(dl, "brand-mark", {x0, y}, ui.S(56), {1, 1, 1, 1});
    dl->AddText(ui.fonts.display, ui.S(30), {x0 + ui.S(72), y + ui.S(2)}, ui::ToU32(c.text), "Breeze");
    const float bw = ui.fonts.display->CalcTextSizeA(ui.S(30), FLT_MAX, 0, "Breeze").x;
    dl->AddText(ui.fonts.display, ui.S(30), {x0 + ui.S(72) + bw, y + ui.S(2)}, ui::ToU32(c.textSecondary), "CAD");
    dl->AddText(ui.fonts.body, ui.S(13), {x0 + ui.S(73), y + ui.S(38)}, ui::ToU32(c.textMuted),
                Trf("Свободный параметрический САПР  \xC2\xB7  версия {}", VersionLine()).c_str());
    y += ui.S(92);

    // Main actions.
    ImGui::SetCursorScreenPos({x0, y});
    // "Создать…" opens the window with every kind; a part and an assembly
    // are one click away beside it (the author's choice).
    if (ui::PrimaryButton(ui, "##home-new", Tr("Создать…"), "Ctrl+N", {ui.S(170), ui.S(40)}))
        app.Later([&app] { OpenNewDocumentDialog(app); });
    ImGui::SameLine(0, ui.S(10));
    if (ui::SecondaryButton(ui, "##home-part", Tr("Деталь"), nullptr, {ui.S(120), ui.S(40)})) app.Later([&app] { NewDocument(app); });
    ImGui::SameLine(0, ui.S(10));
    if (ui::SecondaryButton(ui, "##home-assembly", Tr("Сборка"), nullptr, {ui.S(120), ui.S(40)}))
        app.Later([&app] { NewAssembly(app); });
    ImGui::SameLine(0, ui.S(10));
    if (ui::SecondaryButton(ui, "##home-open", Tr("Открыть…"), "Ctrl+O", {ui.S(170), ui.S(40)})) app.Later([&app] { OpenDocument(app); });
    y += ui.S(72);

    ImGui::SetCursorScreenPos({x0, y});
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(x0 - pos.x + column);

    // Work to recover after a crash.
    if (!app.shell.recoverable.empty()) {
        SectionTitle(ui, Tr("Несохранённая работа"));
        for (std::size_t i = 0; i < app.shell.recoverable.size(); ++i) {
            const RecoverableDocument rec = app.shell.recoverable[i];
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ui.S(52);
            ImDrawList* wdl = ImGui::GetWindowDrawList();
            wdl->AddRectFilled(p, {p.x + column, p.y + h}, ui::ToU32(c.panel), ui.S(10));
            wdl->AddRect(p, {p.x + column, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(10));
            ui.icons.Draw(wdl, "status-warning", {p.x + ui.S(14), p.y + (h - ui.S(18)) / 2}, ui.S(18), {1, 1, 1, 1});
            wdl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(42), p.y + ui.S(10)}, ui::ToU32(c.text), rec.title.c_str());
            const std::string where = Trf("Автосохранение {}", rec.savedAt) + (rec.original.empty() ? "" : "  \xC2\xB7  " + ToUtf8(rec.original));
            const ImVec4 clip{p.x + ui.S(42), p.y, p.x + column - ui.S(250), p.y + h};
            wdl->AddText(ui.fonts.caption, ui.S(11), {p.x + ui.S(42), p.y + ui.S(29)}, ui::ToU32(c.textMuted), where.c_str(),
                         nullptr, 0, &clip);
            ImGui::SetCursorScreenPos({p.x + column - ui.S(236), p.y + ui.S(10)});
            if (ui::PrimaryButton(ui, "##recover", Tr("Восстановить"), nullptr, {ui.S(130), ui.S(32)}))
                app.Later([&app, rec] { RecoverDocument(app, rec); });
            ImGui::SetCursorScreenPos({p.x + column - ui.S(98), p.y + ui.S(10)});
            if (ui::SecondaryButton(ui, "##drop", Tr("Удалить"), nullptr, {ui.S(86), ui.S(32)})) {
                app.Later([&app, rec] {
                    app.autosave.Remove(rec);
                    std::erase_if(app.shell.recoverable, [&](const RecoverableDocument& r) { return r.folder == rec.folder; });
                });
            }
            ImGui::SetCursorScreenPos({p.x, p.y + h + ui.S(8)});
            ImGui::PopID();
        }
        ImGui::Dummy({0, ui.S(18)});
    }

    SectionTitle(ui, Tr("Недавние"));
    if (app.settings.recentFiles.empty()) {
        ui::FontScope font(ui.fonts.body);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::TextUnformatted(Tr("Проектов пока нет. Самое время творить!"));
        ImGui::PopStyleColor();
    }
    for (std::size_t i = 0; i < app.settings.recentFiles.size(); ++i) {
        const std::filesystem::path file = app.settings.recentFiles[i];
        std::error_code ec;
        const bool exists = std::filesystem::exists(file, ec);
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(44);
        const bool clicked = ImGui::InvisibleButton("##recent", {column, h});
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        if (hovered) wdl->AddRectFilled(p, {p.x + column, p.y + h}, ui::ToU32(c.hover), ui.S(8));
        ui.icons.Draw(wdl, "brand-mark", {p.x + ui.S(10), p.y + (h - ui.S(22)) / 2}, ui.S(22), {1, 1, 1, exists ? 1.0f : 0.35f});
        const std::string name = ToUtf8(file.stem());
        wdl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(44), p.y + ui.S(6)}, ui::ToU32(exists ? c.text : c.textDisabled), name.c_str());
        const std::string folder = exists ? ToUtf8(file.parent_path()) : Tr("Файл не найден");
        const ImVec4 clip{p.x + ui.S(44), p.y, p.x + column - ui.S(12), p.y + h};
        wdl->AddText(ui.fonts.caption, ui.S(11), {p.x + ui.S(44), p.y + ui.S(25)}, ui::ToU32(c.textMuted), folder.c_str(), nullptr, 0, &clip);
        if (clicked && exists) app.Later([&app, file] { OpenDocument(app, file); });
        if (clicked && !exists) app.Later([&app, file] { app.settings.RemoveRecent(file); });
        ImGui::PopID();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Dummy({0, ui.S(40)});
    EndArea();
}

} // namespace bz::app
