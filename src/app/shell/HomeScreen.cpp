// Home screen: start a part, open a file, recover unsaved work, recent files.

#include "app/Operations.h"
#include "app/shell/Shell.h"
#include "app/assembly/AssemblyUi.h"
#include "app/shell/FilePreviews.h"
#include "core/I18n.h"
#include "core/Version.h"
#include "core/PathText.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cfloat>
#include <ctime>
#include <filesystem>
#include <format>

namespace bz::app {
namespace {

void SectionTitle(ui::Ui& ui, const char* text) {
    ui::FontScope font(ui.fonts.strong);
    ImGui::TextUnformatted(text);
    ImGui::Dummy({0, ui.S(6)});
}

/// "05.11.2026 17:25" (as the file manager shows it).
std::string DateText(long long modifiedMs) {
    if (modifiedMs <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(modifiedMs / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return std::format("{:02}.{:02}.{} {:02}:{:02}", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
}

bool IsAssembly(const std::filesystem::path& file) { return file.extension() == ".bza"; }

/// The picture (or, until there is one, the kind's icon) fitted into `a`..`b`.
void DrawPicture(ui::Ui& ui, ImDrawList* dl, const FilePreview& pic, bool assembly, ImVec2 a, ImVec2 b, float pad, float alpha) {
    const float aw = b.x - a.x - 2 * pad, ah = b.y - a.y - 2 * pad;
    if (pic.texture && pic.width > 0 && pic.height > 0 && aw > 0 && ah > 0) {
        const float k = std::min(aw / static_cast<float>(pic.width), ah / static_cast<float>(pic.height));
        const float w = static_cast<float>(pic.width) * k, h = static_cast<float>(pic.height) * k;
        const ImVec2 p0{a.x + pad + (aw - w) / 2, a.y + pad + (ah - h) / 2};
        dl->AddImage(static_cast<ImTextureID>(pic.texture), p0, {p0.x + w, p0.y + h}, {0, 0}, {1, 1},
                     IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)));
        return;
    }
    const float is = std::min(ui.S(40), std::min(b.x - a.x, b.y - a.y) * 0.5f);
    ui.icons.Draw(dl, assembly ? "tree-assembly" : "tree-part", {(a.x + b.x - is) / 2, (a.y + b.y - is) / 2}, is,
                  {1, 1, 1, (pic.loading ? 0.35f : 0.6f) * alpha});
}

/// A recent file as a card (picture on top, name and date under it), as in
/// the file managers and other CAD start pages. Returns true when clicked.
bool RecentCard(AppContext& app, const std::filesystem::path& file, ImVec2 p, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const FilePreview pic = FilePreviewOf(file);
    const bool exists = !pic.missing;
    ImGui::SetCursorScreenPos(p);
    const bool clicked = ImGui::InvisibleButton("##card", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ui.S(8);
    const float textH = ui.S(50);
    const float thumbB = p.y + size.y - textH;
    const ImVec2 q{p.x + size.x, p.y + size.y};
    dl->AddRectFilled(p, q, ui::ToU32(held ? c.pressed : hovered ? c.hover : c.panel), r);
    // The picture on the view's own background, rounded at the top only.
    dl->AddRectFilled({p.x + 1, p.y + 1}, {q.x - 1, thumbB}, ui::ToU32(c.viewportBottom), r - 1, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilledMultiColor({p.x + 1, p.y + r}, {q.x - 1, thumbB}, ui::ToU32(c.viewportTop), ui::ToU32(c.viewportTop),
                                ui::ToU32(c.viewportBottom), ui::ToU32(c.viewportBottom));
    dl->AddRectFilled({p.x + 1, p.y + 1}, {q.x - 1, p.y + r}, ui::ToU32(c.viewportTop), r - 1, ImDrawFlags_RoundCornersTop);
    DrawPicture(ui, dl, pic, IsAssembly(file), {p.x, p.y}, {q.x, thumbB}, ui.S(10), exists ? 1.0f : 0.35f);
    dl->AddLine({p.x + 1, thumbB}, {q.x - 1, thumbB}, ui::ToU32(c.divider));
    // The kind in the picture's corner.
    const float ks = ui.S(20);
    const ImVec2 k0{p.x + ui.S(8), thumbB - ks - ui.S(8)};
    dl->AddRectFilled(k0, {k0.x + ks, k0.y + ks}, ui::ToU32(c.panel.WithAlpha(0.9f)), ui.S(5));
    ui.icons.Draw(dl, IsAssembly(file) ? "tree-assembly" : "tree-part", {k0.x + ui.S(3), k0.y + ui.S(3)}, ks - ui.S(6), {1, 1, 1, 1});
    dl->AddRect(p, q, ui::ToU32(hovered ? c.accentSoftBorder : c.divider), r, 0, ui.S(1));
    // The name and the date (or that the file is gone).
    const std::string name = ToUtf8(file.stem());
    ui::FittedText(dl, ui.fonts.medium, ui.S(13), {p.x + ui.S(12), thumbB + ui.S(9)}, q.x - ui.S(10),
                   ui::ToU32(exists ? c.text : c.textDisabled), name.c_str());
    const std::string when = exists ? DateText(pic.modified) : std::string(Tr("Файл не найден"));
    ui::FittedText(dl, ui.fonts.caption, ui.S(11), {p.x + ui.S(12), thumbB + ui.S(28)}, q.x - ui.S(10), ui::ToU32(c.textMuted),
                   when.c_str());
    if (hovered && exists) ui::ItemTooltip(ui, ToUtf8(file).c_str());
    return clicked;
}

/// A recent file as a row: a small picture, the name, the folder and the date.
bool RecentRow(AppContext& app, const std::filesystem::path& file, ImVec2 p, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const FilePreview pic = FilePreviewOf(file);
    const bool exists = !pic.missing;
    const float h = ui.S(52);
    ImGui::SetCursorScreenPos(p);
    const bool clicked = ImGui::InvisibleButton("##row", {width, h});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.hover), ui.S(8));
    const float side = ui.S(42);
    const ImVec2 t0{p.x + ui.S(6), p.y + (h - side) / 2};
    dl->AddRectFilledMultiColor(t0, {t0.x + side, t0.y + side}, ui::ToU32(c.viewportTop), ui::ToU32(c.viewportTop),
                                ui::ToU32(c.viewportBottom), ui::ToU32(c.viewportBottom));
    DrawPicture(ui, dl, pic, IsAssembly(file), t0, {t0.x + side, t0.y + side}, ui.S(2), exists ? 1.0f : 0.35f);
    dl->AddRect(t0, {t0.x + side, t0.y + side}, ui::ToU32(c.divider), ui.S(4));
    const float tx = t0.x + side + ui.S(12);
    const std::string date = exists ? DateText(pic.modified) : std::string();
    const float dw = date.empty() ? 0 : ui.fonts.caption->CalcTextSizeA(ui.S(11), FLT_MAX, 0, date.c_str()).x;
    const float right = p.x + width - ui.S(12) - (dw > 0 ? dw + ui.S(16) : 0);
    const std::string name = ToUtf8(file.stem());
    ui::FittedText(dl, ui.fonts.medium, ui.S(13), {tx, p.y + ui.S(8)}, right, ui::ToU32(exists ? c.text : c.textDisabled), name.c_str());
    const std::string folder = exists ? ToUtf8(file.parent_path()) : Tr("Файл не найден");
    ui::FittedText(dl, ui.fonts.caption, ui.S(11), {tx, p.y + ui.S(28)}, right, ui::ToU32(c.textMuted), folder.c_str());
    if (dw > 0)
        dl->AddText(ui.fonts.caption, ui.S(11), {p.x + width - ui.S(12) - dw, p.y + ui.S(10)}, ui::ToU32(c.textMuted), date.c_str());
    return clicked;
}

} // namespace

void DrawHomeScreen(AppContext& app, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = BeginArea("##home", pos, size, c.ribbon, true);
    // Everything is placed in screen coordinates: the page's scroll moves it.
    const float scroll = ImGui::GetScrollY();

    // Wide enough for the recent files' pictures (six in a row on a usual screen).
    const float column = std::min(size.x - ui.S(64), ui.S(1240));
    const float x0 = pos.x + (size.x - column) / 2;
    float y = pos.y - scroll + std::max(ui.S(48), size.y * 0.07f);

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
    ImGui::PushTextWrapPos(x0 - ImGui::GetWindowPos().x + column);

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

    // "Недавние" and, on the right, pictures or a list.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        SectionTitle(ui, Tr("Недавние"));
        const ImVec2 after = ImGui::GetCursorScreenPos();
        const bool list = app.settings.recentAsList;
        ImGui::SetCursorScreenPos({p.x + column - ui.S(28) * 2 - ui.S(4), p.y - ui.S(6)});
        if (ui::IconButton(ui, "##recent-grid", "ui-grid", 28, 16, Tr("Миниатюры"), true, !list)) app.settings.recentAsList = false;
        ImGui::SameLine(0, ui.S(4));
        if (ui::IconButton(ui, "##recent-list", "ui-list", 28, 16, Tr("Список"), true, list)) app.settings.recentAsList = true;
        ImGui::SetCursorScreenPos(after);
        ImGui::Dummy({0, 0});
    }
    if (app.settings.recentFiles.empty()) {
        ui::FontScope font(ui.fonts.body);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::TextUnformatted(Tr("Проектов пока нет. Самое время творить!"));
        ImGui::PopStyleColor();
    }
    const std::vector<std::filesystem::path> recent = app.settings.recentFiles;   // a click changes the list (later)
    const ImVec2 start = ImGui::GetCursorScreenPos();
    if (!recent.empty() && !app.settings.recentAsList) {
        // As many columns as fit, about 200 px each; the picture 4:3 like the file manager's.
        const float gap = ui.S(14);
        const int cols = std::max(1, static_cast<int>((column + gap) / (ui.S(196) + gap)));
        const float cw = (column - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const float ch = std::round(cw * 0.72f) + ui.S(50);
        for (std::size_t i = 0; i < recent.size(); ++i) {
            const int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
            const ImVec2 p{start.x + static_cast<float>(col) * (cw + gap), start.y + static_cast<float>(row) * (ch + gap)};
            ImGui::PushID(static_cast<int>(i));
            // Off screen: no picture is read for it (nor its file looked at).
            const bool visible = p.y + ch >= pos.y && p.y <= pos.y + size.y;
            bool clicked = false;
            if (visible) clicked = RecentCard(app, recent[i], p, {cw, ch});
            if (clicked) {
                const std::filesystem::path file = recent[i];
                if (FilePreviewOf(file).missing) app.Later([&app, file] { app.settings.RemoveRecent(file); });
                else app.Later([&app, file] { OpenDocument(app, file); });
            }
            ImGui::PopID();
        }
        const int rows = (static_cast<int>(recent.size()) + cols - 1) / cols;
        ImGui::SetCursorScreenPos({start.x, start.y + static_cast<float>(rows) * (ch + gap)});
        ImGui::Dummy({column, 0});   // the extent (ImGui wants an item after moving the cursor)
    } else if (!recent.empty()) {
        float ry = start.y;
        for (std::size_t i = 0; i < recent.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (RecentRow(app, recent[i], {start.x, ry}, column)) {
                const std::filesystem::path file = recent[i];
                if (FilePreviewOf(file).missing) app.Later([&app, file] { app.settings.RemoveRecent(file); });
                else app.Later([&app, file] { OpenDocument(app, file); });
            }
            ImGui::PopID();
            ry += ui.S(54);
        }
        ImGui::SetCursorScreenPos({start.x, ry});
        ImGui::Dummy({column, 0});
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Dummy({0, ui.S(40)});
    EndArea();
}

} // namespace bz::app
