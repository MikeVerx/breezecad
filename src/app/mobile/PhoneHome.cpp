#include "app/mobile/PhoneHome.h"

#include "app/Operations.h"
#include "app/assembly/AssemblyUi.h"
#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "core/Version.h"
#include "platform/Paths.h"
#include "ui/Gl.h"
#include "ui/Widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <fstream>
#include <map>

#if defined(__ANDROID__)
#include "platform/Android.h"
#endif

namespace bz::app::mobile {
namespace {

constexpr int kThumbW = 240, kThumbH = 160;   ///< pixels of a card's picture (3:2)

std::filesystem::path ThumbFile(const std::string& fileName) { return platform::ConfigDir() / "thumbs" / PathFromUtf8(fileName + ".thumb"); }

/// A project shown as a card.
struct Card {
    std::string name;       ///< the file's name, with its extension
    std::string uri;        ///< Android: the folder's document; else empty
    std::filesystem::path path;   ///< the desktop: the file
    long long   modified = 0;     ///< ms since 1970, 0 unknown
    bool        assembly = false;
};

// --- The pictures --------------------------------------------------------------

struct Thumb {
    unsigned texture = 0;
    std::filesystem::file_time_type stamp{};
};
std::map<std::string, Thumb> gThumbs;

/// The card's picture as a texture (0: none yet). Read again when the file changes.
unsigned ThumbTexture(const std::string& fileName) {
    const std::filesystem::path file = ThumbFile(fileName);
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(file, ec);
    Thumb& t = gThumbs[fileName];
    if (ec) return t.texture;   // none: an old one (if any) stays until there is a new
    if (t.texture && t.stamp == stamp) return t.texture;
    t.stamp = stamp;
    std::ifstream in(file, std::ios::binary);
    char magic[4]{};
    std::int32_t w = 0, h = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&w), 4);
    in.read(reinterpret_cast<char*>(&h), 4);
    if (!in || std::string_view(magic, 4) != "BZT1" || w <= 0 || h <= 0 || w > 1024 || h > 1024) return t.texture;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    in.read(reinterpret_cast<char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    if (!in) return t.texture;
    if (!t.texture) glGenTextures(1, &t.texture);
    glBindTexture(GL_TEXTURE_2D, t.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return t.texture;
}

// --- Text ---------------------------------------------------------------------

/// "5 мин назад", "2 ч назад", "вчера", "3 дня назад", "12.10".
std::string WhenText(long long modifiedMs) {
    if (modifiedMs <= 0) return {};
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const long long minutes = std::max<long long>(0, (now - modifiedMs) / 60000);
    if (minutes < 1) return Tr("только что");
    if (minutes < 60) return Trf("{} мин назад", minutes);
    if (minutes < 24 * 60) return Trf("{} ч назад", minutes / 60);
    const long long days = minutes / (24 * 60);
    if (days == 1) return Tr("вчера");
    if (days < 5) return Trf("{} дня назад", days);
    const std::time_t t = static_cast<std::time_t>(modifiedMs / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return std::format("{:02}.{:02}", tm.tm_mday, tm.tm_mon + 1);
}

std::string Stem(const std::string& name) {
    const auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// --- The parts of the page -----------------------------------------------------------

/// A big tile: an icon, a name and a small line under it.
bool Tile(ui::Ui& ui, const char* id, const char* icon, const char* label, const char* sub, ImVec2 size, bool primary) {
    const ui::ThemeColors& c = ui.C();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ui.S(14);
    const Color bg = primary ? c.accentSoft : c.field;
    dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, ui::ToU32(held ? c.pressed : bg), r);
    dl->AddRect(p, {p.x + size.x, p.y + size.y}, ui::ToU32(primary ? c.accentSoftBorder : c.fieldBorder), r, 0, ui.S(1));
    const float is = ui.S(32);
    const float textH = ui.S(14) + (sub ? ui.S(14) : 0);
    const float top = p.y + (size.y - is - ui.S(6) - textH) / 2;
    ui.icons.Draw(dl, icon, {p.x + (size.x - is) / 2, top}, is, {1, 1, 1, 1});
    const ImVec2 ts = ui.fonts.medium->CalcTextSizeA(ui.S(14), FLT_MAX, 0, label);
    dl->AddText(ui.fonts.medium, ui.S(14), {p.x + (size.x - ts.x) / 2, top + is + ui.S(6)}, ui::ToU32(c.text), label);
    if (sub) {
        const ImVec2 ss = ui.fonts.caption->CalcTextSizeA(ui.S(11), FLT_MAX, 0, sub);
        dl->AddText(ui.fonts.caption, ui.S(11), {p.x + (size.x - ss.x) / 2, top + is + ui.S(24)}, ui::ToU32(c.textMuted), sub);
    }
    return clicked;
}

/// A wide button with an icon before the label.
bool WideButton(ui::Ui& ui, const char* id, const char* icon, const char* label, ImVec2 size, bool blue = false) {
    const ui::ThemeColors& c = ui.C();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ui.S(11);
    dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, ui::ToU32(held ? c.pressed : blue ? c.accent : c.panel), r);
    if (!blue) dl->AddRect(p, {p.x + size.x, p.y + size.y}, ui::ToU32(c.fieldBorder), r, 0, ui.S(1));
    const float is = ui.S(16);
    const ImVec2 ts = ui.fonts.medium->CalcTextSizeA(ui.S(13.5f), FLT_MAX, 0, label);
    const float x = p.x + (size.x - is - ui.S(8) - ts.x) / 2;
    ui.icons.Draw(dl, icon, {x, p.y + (size.y - is) / 2}, is, {1, 1, 1, 1});
    dl->AddText(ui.fonts.medium, ui.S(13.5f), {x + is + ui.S(8), p.y + (size.y - ts.y) / 2}, ui::ToU32(blue ? c.textOnAccent : c.text),
                label);
    return clicked;
}

void DrawHero(AppContext& app, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = BeginArea("##home-hero", pos, size, c.ribbon);
    dl->AddLine({pos.x + size.x - 1, pos.y}, {pos.x + size.x - 1, pos.y + size.y}, ui::ToU32(c.divider));
    const float pad = ui.S(18);
    const float x = pos.x + pad, inner = size.x - 2 * pad;
    float y = pos.y + ui.S(16);
    // The brand.
    ui.icons.Draw(dl, "brand-mark", {x, y}, ui.S(32), {1, 1, 1, 1});
    const float fs = ui.S(21);
    dl->AddText(ui.fonts.display, fs, {x + ui.S(42), y - ui.S(2)}, ui::ToU32(c.text), "Breeze");
    const float bw = ui.fonts.display->CalcTextSizeA(fs, FLT_MAX, 0, "Breeze").x;
    dl->AddText(ui.fonts.display, fs, {x + ui.S(42) + bw, y - ui.S(2)}, ui::ToU32(c.textSecondary), "CAD");
    dl->AddText(ui.fonts.caption, ui.S(11), {x + ui.S(43), y + ui.S(22)}, ui::ToU32(c.textMuted),
                Trf("{} · офлайн, без облака", VersionLine()).c_str());
    y += ui.S(50);

    // Part and assembly: one tap. Below them every kind and opening a file.
    const float gap = ui.S(10);
    const float tileW = (inner - gap) / 2;
    const float tileH = std::clamp(size.y * 0.30f, ui.S(80), ui.S(110));
    ImGui::SetCursorScreenPos({x, y});
    if (Tile(ui, "##home-part", "tree-part", Tr("Деталь"), Tr("мм"), {tileW, tileH}, true)) app.Later([&app] { NewDocument(app); });
    ImGui::SetCursorScreenPos({x + tileW + gap, y});
    if (Tile(ui, "##home-assembly", "tree-assembly", Tr("Сборка"), nullptr, {tileW, tileH}, false))
        app.Later([&app] { NewAssembly(app); });
    y += tileH + gap;
    const float bh = ui.S(44);
    ImGui::SetCursorScreenPos({x, y});
    if (WideButton(ui, "##home-new", "ui-plus", Tr("Создать…"), {tileW, bh})) app.Later([&app] { OpenNewDocumentDialog(app); });
    ImGui::SetCursorScreenPos({x + tileW + gap, y});
    if (WideButton(ui, "##home-open", "ui-open", Tr("Открыть…"), {tileW, bh})) app.Later([&app] { OpenDocument(app); });

    // At the bottom: settings and the like, small.
    const float fy = pos.y + size.y - ui.S(38);
    if (fy > y + bh + ui.S(8)) {
        float fx = x;
        for (const char* id : {"app.settings", "app.about", "app.reportProblem"}) {
            const Action* a = app.actions.Find(id);
            if (!a || !a->IsEnabled()) continue;
            std::string label = a->Label();
            if (label.ends_with("…")) label.resize(label.size() - std::string("…").size());
            const ImVec2 ts = ui.fonts.bodySmall->CalcTextSizeA(ui.S(12), FLT_MAX, 0, label.c_str());
            const float w = ui.S(20) + ts.x;
            if (fx + w > x + inner) break;
            ImGui::SetCursorScreenPos({fx, fy});
            ImGui::PushID(id);
            const bool clicked = ImGui::InvisibleButton("##foot", {w, ui.S(30)});
            ImGui::PopID();
            ui.icons.Draw(dl, a->icon, {fx, fy + ui.S(7)}, ui.S(15), {1, 1, 1, 0.8f});
            dl->AddText(ui.fonts.bodySmall, ui.S(12), {fx + ui.S(19), fy + (ui.S(30) - ts.y) / 2},
                        ui::ToU32(clicked || ImGui::IsItemActive() ? c.text : c.textSecondary), label.c_str());
            if (clicked) {
                const std::string run = id;
                app.Later([&app, run] { app.actions.Run(run); });
            }
            fx += w + ui.S(16);
        }
    }
    EndArea();
}

/// Work left by a crash: one line with "Удалить" and "Восстановить".
float DrawRecovery(AppContext& app, ImVec2 at, float width) {
    if (app.shell.recoverable.empty()) return 0;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const RecoverableDocument rec = app.shell.recoverable.front();
    const float h = ui.S(40);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, {at.x + width, at.y + h}, ui::ToU32(c.warning.WithAlpha(0.08f)), ui.S(10));
    dl->AddRect(at, {at.x + width, at.y + h}, ui::ToU32(c.warning.WithAlpha(0.45f)), ui.S(10));
    dl->AddCircleFilled({at.x + ui.S(16), at.y + h / 2}, ui.S(4), ui::ToU32(c.warning));
    const std::string text = Trf("{} — несохранённая работа, автосохранение {}", rec.title, rec.savedAt);
    const float buttons = ui.S(130) + ui.S(84) + ui.S(12);
    const ImVec4 clip{at.x, at.y, at.x + width - buttons, at.y + h};
    dl->AddText(ui.fonts.body, ui.S(12.5f), {at.x + ui.S(28), at.y + (h - ui.S(14)) / 2}, ui::ToU32(c.text), text.c_str(), nullptr, 0,
                &clip);
    ImGui::SetCursorScreenPos({at.x + width - ui.S(130) - ui.S(5), at.y + ui.S(5)});
    if (ui::PrimaryButton(ui, "##home-recover", Tr("Восстановить"), nullptr, {ui.S(130), h - ui.S(10)}))
        app.Later([&app, rec] { RecoverDocument(app, rec); });
    ImGui::SetCursorScreenPos({at.x + width - buttons + ui.S(4), at.y + ui.S(5)});
    if (ui::SecondaryButton(ui, "##home-drop", Tr("Удалить"), nullptr, {ui.S(84), h - ui.S(10)})) {
        app.Later([&app, rec] {
            app.autosave.Remove(rec);
            std::erase_if(app.shell.recoverable, [&](const RecoverableDocument& r) { return r.folder == rec.folder; });
        });
    }
    return h + ui.S(10);
}

/// The projects to show: the folder's (Android) or the recent files.
std::vector<Card> Projects(AppContext& app, bool byName, bool& noFolder) {
    std::vector<Card> cards;
    noFolder = false;
#if defined(__ANDROID__)
    const std::string folder = platform::android::ProjectsFolder();
    noFolder = folder.empty();
    // Read again now and then, not every frame (the system answers slowly).
    static std::vector<platform::android::FolderEntry> entries;
    static double readAt = -100;
    static std::string readFrom;
    if (folder != readFrom || app.now - readAt > 5.0) {
        entries = folder.empty() ? std::vector<platform::android::FolderEntry>{} : platform::android::ListProjects();
        readAt = app.now;
        readFrom = folder;
    }
    for (const auto& e : entries) cards.push_back({e.name, e.uri, {}, e.modified, e.name.ends_with(".bza")});
#else
    for (const auto& file : app.settings.recentFiles) {
        Card card{ToUtf8(file.filename()), {}, file, 0, file.extension() == ".bza"};
        std::error_code ec;
        if (const auto t = std::filesystem::last_write_time(file, ec); !ec) {
            // file_clock → system_clock by the two clocks' "now" (clock_cast is missing in libc++).
            const auto sys = std::chrono::system_clock::now() +
                             std::chrono::duration_cast<std::chrono::system_clock::duration>(t - std::filesystem::file_time_type::clock::now());
            card.modified = std::chrono::duration_cast<std::chrono::milliseconds>(sys.time_since_epoch()).count();
        }
        cards.push_back(std::move(card));
    }
#endif
    if (byName)
        std::sort(cards.begin(), cards.end(), [](const Card& a, const Card& b) { return a.name < b.name; });
    else
        std::stable_sort(cards.begin(), cards.end(), [](const Card& a, const Card& b) { return a.modified > b.modified; });
    return cards;
}

void Open(AppContext& app, const Card& card) {
#if defined(__ANDROID__)
    app.Later([&app, card] {
        if (const auto local = platform::android::Import(card.uri, card.name); !local.empty()) OpenDocument(app, local);
    });
#else
    app.Later([&app, card] { OpenDocument(app, card.path); });
#endif
}

void DrawCard(AppContext& app, const Card& card, ImVec2 p, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImGui::SetCursorScreenPos(p);
    ImGui::PushID(card.name.c_str());
    if (ImGui::InvisibleButton("##card", size)) Open(app, card);
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ui.S(12);
    const float thumbH = size.y - ui.S(42);   // the name and the date under the picture
    dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, ui::ToU32(held ? c.pressed : c.panel), r);
    // The picture, on the view's gradient (or the kind's icon until there is one).
    dl->AddRectFilledMultiColor({p.x + 1, p.y + 1}, {p.x + size.x - 1, p.y + thumbH}, ui::ToU32(c.viewportTop), ui::ToU32(c.viewportTop),
                                ui::ToU32(c.viewportBottom), ui::ToU32(c.viewportBottom));
    if (const unsigned tex = ThumbTexture(card.name)) {
        // Fit 3:2 into the area, centred.
        const float aw = size.x - 2, ah = thumbH - 1;
        float w = aw, h = aw * kThumbH / kThumbW;
        if (h > ah) {
            h = ah;
            w = ah * kThumbW / kThumbH;
        }
        const ImVec2 a{p.x + 1 + (aw - w) / 2, p.y + 1 + (ah - h) / 2};
        dl->AddImage(static_cast<ImTextureID>(tex), a, {a.x + w, a.y + h});
    } else {
        const float is = ui.S(34);
        ui.icons.Draw(dl, card.assembly ? "tree-assembly" : "tree-part", {p.x + (size.x - is) / 2, p.y + (thumbH - is) / 2}, is,
                      {1, 1, 1, 0.75f});
    }
    dl->AddRect(p, {p.x + size.x, p.y + size.y}, ui::ToU32(c.divider), r, 0, ui.S(1));
    // The kind in the corner.
    const char* kind = card.assembly ? Tr("Сборка") : Tr("Деталь");
    const ImVec2 ks = ui.fonts.caption->CalcTextSizeA(ui.S(10), FLT_MAX, 0, kind);
    const ImVec2 k0{p.x + ui.S(7), p.y + ui.S(7)};
    dl->AddRectFilled(k0, {k0.x + ks.x + ui.S(10), k0.y + ks.y + ui.S(3)}, ui::ToU32(Color{0, 0, 0, 0.35f}), ui.S(5));
    dl->AddText(ui.fonts.caption, ui.S(10), {k0.x + ui.S(5), k0.y + ui.S(1.5f)}, ui::ToU32(c.textSecondary), kind);
    // The name and when.
    const std::string name = Stem(card.name);
    ui::FittedText(dl, ui.fonts.medium, ui.S(12.5f), {p.x + ui.S(9), p.y + thumbH + ui.S(5)}, p.x + size.x - ui.S(8), ui::ToU32(c.text),
                   name.c_str());
    const std::string when = WhenText(card.modified);
    dl->AddText(ui.fonts.caption, ui.S(10.5f), {p.x + ui.S(9), p.y + thumbH + ui.S(5) + ui.S(17)}, ui::ToU32(c.textMuted), when.c_str());
}

void DrawProjects(AppContext& app, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    BeginArea("##home-projects", pos, size, c.appBackground);
    const float pad = ui.S(16);
    const float x = pos.x + pad, inner = size.x - 2 * pad;
    float y = pos.y + ui.S(12);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // The header: "Проекты", the folder, recent or all, search.
    static int order = 0;   // 0 newest first, 1 by name
    static bool searching = false;
    static std::string filter;
    const float rowH = ui.S(32);
    dl->AddText(ui.fonts.title, ui.S(16), {x, y + (rowH - ui.S(18)) / 2}, ui::ToU32(c.text), Tr("Проекты"));
    float hx = x + ui.fonts.title->CalcTextSizeA(ui.S(16), FLT_MAX, 0, Tr("Проекты")).x + ui.S(12);
    const float right = x + inner;
    // From the right: search, then the order.
    ImGui::SetCursorScreenPos({right - rowH, y});
    if (ui::IconButton(ui, "##home-search", "ui-search", 32, 16, Tr("Поиск"), true, searching)) {
        searching = !searching;
        if (!searching) filter.clear();
    }
    static constexpr std::array<const char*, 2> orders{N_("Недавние"), N_("Все по имени")};
    const float segW = ui.S(190);
    ImGui::SetCursorScreenPos({right - rowH - ui.S(8) - segW, y});
    order = ui::Segmented(ui, "home-order", orders, order, 2, segW);
#if defined(__ANDROID__)
    // The folder, as a chip: its name and "сменить".
    {
        const std::string folderName = app.settings.mobile.projectsFolderName.empty()
                                           ? std::string(platform::android::ProjectsFolder().empty() ? Tr("Папка не выбрана") : Tr("Папка проектов"))
                                           : app.settings.mobile.projectsFolderName;
        const std::string change = platform::android::ProjectsFolder().empty() ? Tr("выбрать") : Tr("сменить");
        const float room = right - rowH - ui.S(16) - segW - hx;
        if (room > ui.S(110)) {
            const ImVec2 ns = ui.fonts.bodySmall->CalcTextSizeA(ui.S(12), FLT_MAX, 0, folderName.c_str());
            const ImVec2 cs = ui.fonts.bodySmall->CalcTextSizeA(ui.S(12), FLT_MAX, 0, change.c_str());
            const float w = std::min(room, ui.S(40) + ns.x + ui.S(14) + cs.x);
            ImGui::SetCursorScreenPos({hx, y});
            const bool clicked = ImGui::InvisibleButton("##home-folder", {w, rowH});
            dl->AddRectFilled({hx, y}, {hx + w, y + rowH}, ui::ToU32(ImGui::IsItemActive() ? c.pressed : c.field), rowH / 2);
            dl->AddRect({hx, y}, {hx + w, y + rowH}, ui::ToU32(c.fieldBorder), rowH / 2);
            ui.icons.Draw(dl, "ui-folder", {hx + ui.S(12), y + (rowH - ui.S(14)) / 2}, ui.S(14), {1, 1, 1, 0.8f});
            const float tx = hx + ui.S(32);
            const float cx = hx + w - ui.S(12) - cs.x;
            ui::FittedText(dl, ui.fonts.bodySmall, ui.S(12), {tx, y + (rowH - ns.y) / 2}, cx - ui.S(8), ui::ToU32(c.textSecondary),
                           folderName.c_str());
            dl->AddText(ui.fonts.bodySmall, ui.S(12), {cx, y + (rowH - cs.y) / 2}, ui::ToU32(c.selection), change.c_str());
            if (clicked) platform::android::ChooseProjectsFolder();
        }
    }
#else
    (void)hx;
#endif
    y += rowH + ui.S(10);
    if (searching) {
        ImGui::SetCursorScreenPos({x, y});
        ui::TextField(ui, "##home-filter", filter, inner, Tr("Имя проекта"));
        y = ImGui::GetCursorScreenPos().y + ui.S(8);
    }
    y += DrawRecovery(app, {x, y}, inner);

    // The cards: as many columns as fit (about 140 dp each), rows scroll with the page.
    bool noFolder = false;
    std::vector<Card> cards = Projects(app, order == 1, noFolder);
    if (!filter.empty()) {
        std::string f = filter;
        std::erase_if(cards, [&](const Card& card) { return card.name.find(f) == std::string::npos; });
    }
    if (cards.empty()) {
        ImGui::SetCursorScreenPos({x, y + ui.S(10)});
        ImGui::PushTextWrapPos(x + inner);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::TextWrapped("%s", noFolder ? Tr("Выберите папку проектов один раз — детали и сборки будут сохраняться в неё и открываться "
                                               "отсюда без лишних окон.")
                                          : !filter.empty() ? Tr("Ничего не найдено.")
                                                            : Tr("Проектов пока нет. Самое время творить!"));
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
#if defined(__ANDROID__)
        if (noFolder) {
            ImGui::Dummy({0, ui.S(8)});
            ImGui::SetCursorScreenPos({x, ImGui::GetCursorScreenPos().y});
            if (ui::PrimaryButton(ui, "##home-choose-folder", Tr("Выбрать папку…"), nullptr, ui.S(170, 40)))
                platform::android::ChooseProjectsFolder();
        }
#endif
    } else {
        const float gap = ui.S(10);
        const int cols = std::max(2, static_cast<int>((inner + gap) / (ui.S(140) + gap)));
        const float cw = (inner - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const float ch = std::round(cw * 0.58f + ui.S(42));
        for (std::size_t i = 0; i < cards.size(); ++i) {
            const int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
            DrawCard(app, cards[i], {x + static_cast<float>(col) * (cw + gap), y + static_cast<float>(row) * (ch + gap)}, {cw, ch});
        }
        const int rows = (static_cast<int>(cards.size()) + cols - 1) / cols;
        ImGui::SetCursorScreenPos({x, y + static_cast<float>(rows) * (ch + gap)});
        ImGui::Dummy({inner, ui.S(12)});
    }
    EndArea();
}

} // namespace

void DrawPhoneHome(AppContext& app, ImVec2 pos, ImVec2 size) {
    const float heroW = std::clamp(size.x * 0.34f, app.ui.S(240), app.ui.S(320));
    DrawHero(app, pos, {heroW, size.y});
    DrawProjects(app, {pos.x + heroW, pos.y}, {size.x - heroW, size.y});
}

void SaveThumbnail(AppContext& app, const Session& session) {
    if (session.path.empty()) return;
    // The view as it is on the screen, small (3:2, like the cards).
    const std::optional<Image> shot = app.viewport.Snapshot(app.viewport.Camera(), kThumbW, kThumbH);
    if (!shot || shot->width <= 0) return;
    const std::filesystem::path file = ThumbFile(ToUtf8(session.path.filename()));
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const std::int32_t w = shot->width, h = shot->height;
    out.write("BZT1", 4);
    out.write(reinterpret_cast<const char*>(&w), 4);
    out.write(reinterpret_cast<const char*>(&h), 4);
    out.write(reinterpret_cast<const char*>(shot->rgba.data()), static_cast<std::streamsize>(shot->rgba.size()));
    if (!out) log::Warn("Home: the picture of {} was not saved", ToUtf8(session.path.filename()));
}

} // namespace bz::app::mobile
