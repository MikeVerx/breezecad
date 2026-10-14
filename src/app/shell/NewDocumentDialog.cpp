// The "Создать" window (Inventor's New File): folders on the left, the kinds
// of documents of the chosen folder as tiles, templates (a part in mm or in
// inches). Built from doc::DocumentTypeRegistry through NewDocumentGroups, so
// a kind a module registers later appears here by itself. The last template
// used is chosen when the window opens: Enter creates it at once.

#include "app/NewTemplates.h"
#include "app/Operations.h"
#include "app/assembly/AssemblyUi.h"
#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <algorithm>

namespace bz::app {

void OpenNewDocumentDialog(AppContext& app) {
    auto& nd = app.shell.newDocument;
    const std::vector<NewGroup> groups = NewDocumentGroups(doc::DocumentTypeRegistry::Default());
    const NewTemplate* start =
        InitialTemplate(groups, app.settings.lastNewTemplate, static_cast<LengthUnit>(app.settings.newPartLength));
    nd.chosen = start ? start->id : std::string{};
    nd.group = {};
    for (const NewGroup& g : groups)
        if (start && std::ranges::any_of(g.templates, [&](const NewTemplate& t) { return t.id == start->id; })) nd.group = g.name;
    if (nd.group.empty() && !groups.empty()) nd.group = groups.front().name;
    nd.open = true;
}

namespace {

void CreateFromTemplate(AppContext& app, const NewTemplate& t) {
    if (!t.available) return;
    if (t.type == doc::kAssemblyType) {
        NewAssembly(app);
    } else if (t.type == doc::kPartType) {
        NewDocument(app);
        if (Session* s = app.Doc(); s && t.length) s->doc.units.length = *t.length;
    } else {
        return;   // a kind this version cannot make yet (a module's, later)
    }
    app.settings.lastNewTemplate = t.id;
}

/// One tile: icon, title, note; grey and "позже" when not available.
bool Tile(ui::Ui& ui, const NewTemplate& t, bool chosen, float w, float h, bool& doubleClicked) {
    const ui::ThemeColors& c = ui.C();
    ImGui::PushID(t.id.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##tile", {w, h}) && t.available;
    const bool hovered = ImGui::IsItemHovered() && t.available;
    doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ui.S(10);
    dl->AddRectFilled(p, {p.x + w, p.y + h}, ui::ToU32(chosen ? c.accentSoft : hovered ? c.hover : c.panel), r);
    dl->AddRect(p, {p.x + w, p.y + h}, ui::ToU32(chosen ? c.accentSoftBorder : c.fieldBorder), r, 0, chosen ? ui.S(1.5f) : ui.S(1.0f));
    const float icon = ui.S(40);
    ui.icons.Draw(dl, t.icon.empty() ? "ui-new" : t.icon, {p.x + (w - icon) / 2, p.y + ui.S(16)}, icon,
                  {1, 1, 1, t.available ? 1.0f : 0.35f});
    const std::string title = Tr(t.title);
    const ImVec2 ts = ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, title.c_str());
    dl->AddText(ui.fonts.medium, ui.S(13), {p.x + (w - ts.x) / 2, p.y + ui.S(64)}, ui::ToU32(t.available ? c.text : c.textDisabled),
                title.c_str());
    const std::string note = t.available ? Tr(t.note) : std::string(Tr("позже"));
    if (!note.empty()) {
        const ImVec2 ns = ui.fonts.caption->CalcTextSizeA(ui.S(11), FLT_MAX, 0, note.c_str());
        dl->AddText(ui.fonts.caption, ui.S(11), {p.x + (w - ns.x) / 2, p.y + ui.S(84)}, ui::ToU32(c.textMuted), note.c_str());
    }
    ImGui::PopID();
    return clicked;
}

} // namespace

void DrawNewDocumentDialog(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    auto& nd = app.shell.newDocument;
    const char* id = "##new-document";
    if (nd.open && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({ui.S(760), 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(24, 22));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ui.S(8, 8));
    const bool visible = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    if (!visible) {
        nd.open = false;
        return;
    }
    const auto close = [&] {
        nd.open = false;
        ImGui::CloseCurrentPopup();
    };
    const std::vector<NewGroup> groups = NewDocumentGroups(doc::DocumentTypeRegistry::Default());
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(Tr("Создать"));
    }
    ImGui::Dummy({0, ui.S(4)});

    // Folders on the left.
    const float listW = ui.S(170), height = ui.S(250);
    ImGui::BeginGroup();
    for (const NewGroup& g : groups) {
        ImGui::PushID(g.name.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(36);
        const bool clicked = ImGui::InvisibleButton("##group", {listW, h});
        const bool hovered = ImGui::IsItemHovered();
        const bool current = nd.group == g.name;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (current || hovered) dl->AddRectFilled(p, {p.x + listW, p.y + h}, ui::ToU32(current ? c.accentSoft : c.hover), ui.S(8));
        const bool live = g.Available();
        dl->AddText(current ? ui.fonts.medium : ui.fonts.body, ui.S(13), {p.x + ui.S(12), p.y + (h - ui.S(13)) / 2 - ui.S(1)},
                    ui::ToU32(live ? c.text : c.textDisabled), Tr(g.name).c_str());
        if (!live) {
            const char* later = Tr("позже");
            const ImVec2 s = ui.fonts.caption->CalcTextSizeA(ui.S(11), FLT_MAX, 0, later);
            dl->AddText(ui.fonts.caption, ui.S(11), {p.x + listW - s.x - ui.S(10), p.y + (h - ui.S(11)) / 2}, ui::ToU32(c.textMuted), later);
        }
        if (clicked && nd.group != g.name) {
            nd.group = g.name;
            // The folder's first kind becomes the choice, so "Создать" makes what is shown.
            for (const NewTemplate& t : g.templates)
                if (t.available) {
                    nd.chosen = t.id;
                    break;
                }
        }
        ImGui::PopID();
    }
    ImGui::Dummy({listW, std::max(0.0f, height - static_cast<float>(groups.size()) * ui.S(44))});
    ImGui::EndGroup();
    ImGui::SameLine(0, ui.S(20));

    // Tiles of the folder.
    const NewTemplate* create = nullptr;
    ImGui::BeginGroup();
    const auto group = std::ranges::find(groups, nd.group, &NewGroup::name);
    const float tileW = ui.S(150), tileH = ui.S(110), gap = ui.S(12);
    const float area = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, static_cast<int>((area + gap) / (tileW + gap)));
    if (group != groups.end()) {
        for (std::size_t i = 0; i < group->templates.size(); ++i) {
            const NewTemplate& t = group->templates[i];
            if (i % static_cast<std::size_t>(perRow) != 0) ImGui::SameLine(0, gap);
            bool twice = false;
            if (Tile(ui, t, nd.chosen == t.id, tileW, tileH, twice)) nd.chosen = t.id;
            if (twice) create = &t;
        }
        if (!group->Available()) {
            ImGui::Dummy({0, ui.S(6)});
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + area);
            ImGui::TextUnformatted(Tr("Этот вид документов появится в следующих версиях."));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndGroup();

    // Footer.
    ImGui::Dummy({0, ui.S(10)});
    ui::Divider(ui, c.dividerSoft);
    ImGui::Dummy({0, ui.S(4)});
    const NewTemplate* chosen = FindTemplate(groups, nd.chosen);
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    {
        ui::FontScope font(ui.fonts.bodySmall);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textSecondary));
        if (chosen && chosen->available)
            ImGui::TextUnformatted(chosen->note.empty() ? Tr(chosen->title).c_str()
                                                        : (Tr(chosen->title) + " \xC2\xB7 " + Tr(chosen->note)).c_str());
        ImGui::PopStyleColor();
    }
    const float bw = ui.S(150);
    ImGui::SameLine(right - 2 * bw - ui.S(8));
    if (ui::SecondaryButton(ui, "##new-cancel", Tr("Отмена"), "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
    ImGui::SameLine(0, ui.S(8));
    const bool can = chosen && chosen->available;
    if ((ui::PrimaryButton(ui, "##new-create", Tr("Создать"), "Enter", {bw, ui.S(36)}, can) || (can && ui::EnterPressed())) &&
        can)
        create = chosen;
    if (create && create->available) {
        const NewTemplate made = *create;
        close();
        app.Later([&app, made] { CreateFromTemplate(app, made); });
    }
    ImGui::EndPopup();
}

} // namespace bz::app
