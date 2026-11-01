#include "app/assembly/AssemblyIssues.h"

#include "app/AssemblyState.h"
#include "app/Session.h"
#include "assembly/AssemblyFile.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <map>

namespace bz::app {
namespace {

using Issue = AssemblyState::Issue;

/// The list behind one counter (red or yellow).
void IssueList(AppContext& app, Session& session, bool red, const std::vector<Issue>& issues) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    AssemblyState& st = *session.assembly;
    const float w = ui.S(380);
    ImGui::Dummy({w, 0});
    {
        ui::FontScope font(ui.fonts.strong);
        ImGui::TextUnformatted(red ? Tr("Файлы деталей") : Tr("Ошибки сборки"));
    }
    ImGui::Separator();
    bool anyMissing = false;
    for (const Issue& i : issues) {
        if (i.Red() != red) continue;
        anyMissing = anyMissing || i.kind == Issue::Kind::MissingFile;
        ImGui::PushID(static_cast<int>(i.id.value));
        {
            ui::FontScope font(ui.fonts.medium);
            ImGui::TextUnformatted(i.name.c_str());
        }
        {
            ui::FontScope font(ui.fonts.caption);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
            ImGui::TextUnformatted(Tr(i.detail).c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (red) {
            if (ui::SecondaryButton(ui, "##pick", i.kind == Issue::Kind::MissingFile ? Tr("Указать файл детали…") : Tr("Заменить другим файлом…"),
                                    nullptr, {w, ui.S(28)})) {
                const ObjectId id = i.id;
                const auto file = platform::ShowOpenDialog(Tr("Файл детали"), {{Tr("Деталь BreezeCAD"), "*.bzc"}},
                                                           session.path.empty() ? platform::DocumentsDir() : session.path.parent_path());
                if (file) app.Later([&app, &session, id, f = *file] { ReplacePart(app, session, id, f); });
            }
        } else if (ui::SecondaryButton(ui, "##show", Tr("Показать в дереве"), nullptr, {w, ui.S(28)})) {
            st.selected = i.id;
            app.shell.leftTab = LeftTab::Tree;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Dummy({0, ui.S(4)});
        ImGui::PopID();
    }
    if (anyMissing) {
        ImGui::Separator();
        // The usual case after copying a project: every part is there, in
        // another folder. One folder answers for all of them.
        if (ui::SecondaryButton(ui, "##search", Tr("Найти все в папке…"), nullptr, {w, ui.S(30)})) {
            const auto folder = platform::ShowFolderDialog(Tr("Папка с деталями"), session.path.empty() ? platform::DocumentsDir()
                                                                                                         : session.path.parent_path());
            if (folder)
                app.Later([&app, &session, f = *folder] {
                    const int found = FindMissingParts(app, session, f);
                    if (found > 0) app.notes.Success(Trf("Найдено деталей: {}", found));
                    else app.notes.Error(Tr("В этой папке нет деталей с такими именами"));
                });
        }
    }
}

} // namespace

bool ReplacePart(AppContext& app, Session& session, ObjectId component, const std::filesystem::path& file) {
    if (!session.assembly) return false;
    AssemblyState& st = *session.assembly;
    assembly::Component* c = st.assembly.FindComponent(component);
    if (!c) return false;
    const Expected<std::string> type = doc::ReadDocumentType(file);
    if (!type || *type != doc::kPartType) {
        app.notes.Error(Trf("«{}» — не деталь BreezeCAD", ToUtf8(file.filename())));
        return false;
    }
    st.Checkpoint(N_("Замена детали"));
    c->path = session.path.empty() ? ToUtf8(file) : assembly::RelativePartPath(session.path, file);
    st.assembly.Touch();
    log::Info("Assembly: component {} now uses {}", c->name, ToUtf8(file));
    return true;
}

int FindMissingParts(AppContext& app, Session& session, const std::filesystem::path& folder) {
    if (!session.assembly) return 0;
    AssemblyState& st = *session.assembly;
    // The parts in that folder and below, by file name (the first found wins).
    std::map<std::u8string, std::filesystem::path> byName;
    std::error_code ec;
    int seen = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator() && seen < 50000; it.increment(ec), ++seen) {
        if (it.depth() > 6) it.disable_recursion_pending();
        if (!it->is_regular_file(ec) || it->path().extension() != ".bzc") continue;
        byName.emplace(it->path().filename().u8string(), it->path());
    }
    int found = 0;
    std::vector<std::pair<ObjectId, std::filesystem::path>> fixes;
    for (const Issue& i : st.Issues()) {
        if (i.kind != Issue::Kind::MissingFile) continue;
        const assembly::Component* c = st.assembly.FindComponent(i.id);
        if (!c) continue;
        const std::filesystem::path wanted = assembly::ResolvePartPath(session.path, c->path).filename();
        if (const auto f = byName.find(wanted.u8string()); f != byName.end()) fixes.emplace_back(i.id, f->second);
    }
    for (const auto& [id, file] : fixes)
        if (ReplacePart(app, session, id, file)) ++found;
    return found;
}

float DrawIssueCounters(AppContext& app, float right, float centreY) {
    Session* session = app.workspace.ShowsHome() ? nullptr : app.workspace.ActiveTab();
    if (!session || !session->assembly) return 0;
    const std::vector<Issue> issues = session->assembly->Issues();
    int red = 0, yellow = 0;
    for (const Issue& i : issues) (i.Red() ? red : yellow)++;
    if (red == 0 && yellow == 0) return 0;

    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = right;
    const float h = ui.S(24);
    // Right to left: yellow, then red (red reads first).
    for (const bool isRed : {false, true}) {
        const int n = isRed ? red : yellow;
        if (n == 0) continue;
        const std::string count = std::to_string(n);
        const float tw = ui.fonts.medium->CalcTextSizeA(ui.S(12), FLT_MAX, 0, count.c_str()).x;
        const float w = ui.S(8 + 16 + 5 + 9) + tw;
        x -= w;
        const ImVec2 lo{x, centreY - h / 2}, hi{x + w, centreY + h / 2};
        const Color& tone = isRed ? c.danger : c.warning;
        ImGui::SetCursorScreenPos(lo);
        const char* id = isRed ? "##issues-red" : "##issues-yellow";
        if (ImGui::InvisibleButton(id, {w, h})) ImGui::OpenPopup(isRed ? "##issues-red-list" : "##issues-yellow-list");
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", isRed ? Trf("Деталей не найдено или не читается: {}", n).c_str()
                                          : Trf("Ошибок сборки: {}", n).c_str());
        }
        dl->AddRectFilled(lo, hi, ui::ToU32(tone.WithAlpha(hovered ? 0.22f : 0.14f)), h / 2);
        dl->AddRect(lo, hi, ui::ToU32(tone.WithAlpha(0.55f)), h / 2);
        ui.icons.Draw(dl, isRed ? "status-error" : "status-warning", {lo.x + ui.S(6), centreY - ui.S(8)}, ui.S(16), tone);
        dl->AddText(ui.fonts.medium, ui.S(12), {lo.x + ui.S(6 + 16 + 5), centreY - ui.S(7)}, ui::ToU32(tone), count.c_str());
        x -= ui.S(6);
    }
    for (const bool isRed : {true, false}) {
        ui::MenuStyle style(ui);
        if (ImGui::BeginPopup(isRed ? "##issues-red-list" : "##issues-yellow-list")) {
            IssueList(app, *session, isRed, issues);
            ImGui::EndPopup();
        }
    }
    return right - x;
}

} // namespace bz::app
