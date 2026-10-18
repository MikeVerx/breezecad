// The library on the phone (docs/dev/mockups 6a, 6b; the author 17.10.2026):
// full screen like the phone settings. 6a: folders · standards · the part,
// sizes as tiles with a search, the length on the numpad, one wide button.
// 6b: «В мою библиотеку» — the part's variables as pills, the table of
// sizes as cells (the numpad), one wide «Сохранить в библиотеку».
//
// The same library and the same actions as the desktop window
// (shell/Dialogs.cpp, shell/SaveToLibrary.cpp); only the layout differs.

#include "app/Operations.h"
#include "app/assembly/AssemblyUi.h"
#include "app/mobile/MobileLayout.h"
#include "app/shell/Shell.h"
#include "core/Expression.h"
#include "core/I18n.h"
#include "document/StandardPacks.h"
#include "document/StandardParts.h"
#include "ui/Widgets.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>

namespace bz::app {
namespace {

namespace parts = doc::parts;

/// The full-screen window next to the hotbar, as the phone settings.
bool BeginFullScreen(AppContext& app, const char* id, ImVec2& pos, ImVec2& size) {
    const ui::ThemeColors& c = app.ui.C();
    const mobile::MobileState& ms = app.shell.mobile;
    const ImGuiIO& io = ImGui::GetIO();
    const float left = ms.rHotbar.max.x > 0 && ms.rHotbar.min.x < 1 ? ms.rHotbar.max.x : ms.insets.left;
    const float right = io.DisplaySize.x - (ms.rHotbar.min.x > 1 ? io.DisplaySize.x - ms.rHotbar.min.x : ms.insets.right);
    pos = {left, 0};
    size = {right - left, io.DisplaySize.y};
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(c.appBackground));
    const bool open = ImGui::Begin(id, nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    return open;
}

/// A rounded box with text: a tile, a card, a pill. True when tapped.
bool Box(ui::Ui& ui, const char* id, ImVec2 a, ImVec2 b, bool on, float radius) {
    const ui::ThemeColors& c = ui.C();
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton(id, {b.x - a.x, b.y - a.y});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, ui::ToU32(ImGui::IsItemActive() ? c.pressed : on ? c.accentSoft : c.panel), radius);
    dl->AddRect(a, b, ui::ToU32(on ? c.accentSoftBorder : c.fieldBorder), radius, 0, ui.S(1));
    return clicked;
}

void BoxText(ui::Ui& ui, ImVec2 a, ImVec2 b, const char* text, Color ink, bool centred, ImFont* font = nullptr, float size = 0) {
    font = font ? font : ui.fonts.medium;
    size = size > 0 ? size : ui.S(13);
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    const float x = centred ? std::max(a.x + ui.S(8), (a.x + b.x - ts.x) / 2) : a.x + ui.S(14);
    ImGui::PushClipRect(a, b, true);
    ImGui::GetWindowDrawList()->AddText(font, size, {x, (a.y + b.y - ts.y) / 2}, ui::ToU32(ink), text);
    ImGui::PopClipRect();
}

/// The wide primary button at the bottom of a column.
bool WideButton(ui::Ui& ui, const char* id, const char* icon, const char* label, ImVec2 a, ImVec2 b, bool enabled = true) {
    const ui::ThemeColors& c = ui.C();
    ImGui::SetCursorScreenPos(a);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, {b.x - a.x, b.y - a.y});
    ImGui::EndDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, ui::ToU32(!enabled ? c.field : ImGui::IsItemActive() ? c.accentHover : c.accent), ui.S(12));
    const ImVec2 ts = ui.fonts.strong->CalcTextSizeA(ui.S(14), FLT_MAX, 0, label);
    const float is = ui.S(18), x = (a.x + b.x - ts.x - is - ui.S(8)) / 2;
    const Color ink = enabled ? c.textOnAccent : c.textDisabled;
    ui.icons.Draw(dl, icon, {x, (a.y + b.y - is) / 2}, is, ink);
    dl->AddText(ui.fonts.strong, ui.S(14), {x + is + ui.S(8), (a.y + b.y - ts.y) / 2}, ui::ToU32(ink), label);
    return clicked && enabled;
}

/// The red cross at the top right of a full-screen window.
bool CloseCross(ui::Ui& ui, const char* id, ImVec2 at) {
    const ui::ThemeColors& c = ui.C();
    const float s = ui.S(36);
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton(id, {s, s});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, {at.x + s, at.y + s}, ui::ToU32(c.danger), ui.S(10));
    ui.icons.Draw(dl, "ui-close", {at.x + (s - ui.S(18)) / 2, at.y + (s - ui.S(18)) / 2}, ui.S(18), c.textOnAccent);
    return clicked;
}

/// A caption in small muted letters.
void Caption(ui::Ui& ui, ImVec2 at, const char* text) {
    ImGui::GetWindowDrawList()->AddText(ui.fonts.body, ui.S(11.5f), at, ui::ToU32(ui.C().textMuted), text);
}

/// A text field for words (names, a search): the system keyboard, not the numpad.
bool WordsField(AppContext& app, const char* id, const char* hint, std::string& text, ImVec2 at, float width) {
    ImGui::SetCursorScreenPos(at);
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, app.ui.S(12, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, app.ui.S(10));
    const bool changed = ImGui::InputTextWithHint(id, hint, &text);
    ImGui::PopStyleVar(2);
    if (ImGui::IsItemActive()) app.shell.mobile.fullKeyboard = true;   // Application_android: the system keyboard
    return changed;
}

/// The numpad over the bottom of a column while a value is typed (block 1,
/// where it lives otherwise, is under this window).
void NumpadOver(AppContext& app, ImVec2 colMin, ImVec2 colMax) {
    mobile::MobileState& m = app.shell.mobile;
    if (!m.numpad) return;
    const float h = mobile::NumpadHeight(app.ui);
    const ImVec2 a{colMin.x, std::max(colMin.y, colMax.y - h)};
    ImGui::GetForegroundDrawList()->AddRectFilled(a, colMax, ui::ToU32(app.ui.C().appBackground));
    ImGui::PushClipRect({0, 0}, ImGui::GetIO().DisplaySize, false);
    mobile::DrawNumpad(app, a, {colMax.x - a.x, colMax.y - a.y});
    ImGui::PopClipRect();
}

/// Groups of the window's tab (ГОСТ / ISO / DIN) that have something in it.
std::vector<int> VisibleGroups(int family) {
    std::vector<int> out;
    const auto groups = parts::Groups();
    for (int g = 0; g < static_cast<int>(groups.size()); ++g)
        for (parts::PartKind k : groups[static_cast<std::size_t>(g)].kinds) {
            const auto items = parts::StandardsOf(k);
            if (std::ranges::any_of(items, [&](const parts::StandardInfo* s) {
                    return static_cast<int>(parts::FamilyOf(*s)) == family || parts::InEveryFamily(*s);
                })) {
                out.push_back(g);
                break;
            }
        }
    return out;
}

} // namespace

// --- 6a: the library ------------------------------------------------------------

void DrawPhoneLibrary(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    auto& lib = app.shell.library;
    const mobile::MobileState& ms = app.shell.mobile;
    ImVec2 pos, size;
    if (!BeginFullScreen(app, "##phone-library", pos, size)) {
        ImGui::End();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float top = pos.y + std::max(ui.S(12), ms.insets.top), bottom = pos.y + size.y - std::max(ui.S(12), ms.insets.bottom);
    const float leftW = std::clamp(size.x * 0.25f, ui.S(170), ui.S(214)), midW = std::clamp(size.x * 0.23f, ui.S(160), ui.S(200));
    const float x1 = pos.x + leftW, x2 = x1 + midW;
    dl->AddRectFilled(pos, {x1, pos.y + size.y}, ui::ToU32(c.ribbon));
    dl->AddLine({x1, pos.y}, {x1, pos.y + size.y}, ui::ToU32(c.divider));
    dl->AddLine({x2, pos.y}, {x2, pos.y + size.y}, ui::ToU32(c.divider));
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);   // «Назад»

    // --- Left: the title, ГОСТ / ISO / DIN, the folders, the add button ---
    const float lx = pos.x + ui.S(12), lw = leftW - ui.S(24);
    dl->AddText(ui.fonts.title, ui.S(17), {lx + ui.S(2), top}, ui::ToU32(c.text), Tr("Библиотека"));
    float ly = top + ui.S(32);
    {
        static constexpr std::array<const char*, 3> kFamilies{"ГОСТ", "ISO", "DIN"};
        const float w = (lw - ui.S(8)) / 3;
        for (int i = 0; i < 3; ++i) {
            const ImVec2 a{lx + static_cast<float>(i) * (w + ui.S(4)), ly}, b{a.x + w, ly + ui.S(32)};
            ImGui::PushID(i);
            if (Box(ui, "##family", a, b, lib.family == i, ui.S(9))) lib.family = i;
            ImGui::PopID();
            BoxText(ui, a, b, kFamilies[static_cast<std::size_t>(i)], lib.family == i ? c.text : c.textSecondary, true);
        }
        ly += ui.S(40);
    }
    const std::vector<int> groups = VisibleGroups(lib.family);
    if (std::ranges::find(groups, lib.phoneGroup) == groups.end()) lib.phoneGroup = groups.empty() ? -1 : groups.front();
    const float addH = ui.S(40);
    ImGui::SetCursorScreenPos({lx, ly});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ui.S(3));
    ImGui::BeginChild("##phone-lib-folders", {lw, bottom - addH - ui.S(8) - ly}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    for (const int g : groups) {
        const float fw = ImGui::GetContentRegionAvail().x - ui.S(4);
        const ImVec2 a = ImGui::GetCursorScreenPos(), b{a.x + fw, a.y + ui.S(40)};
        ImGui::PushID(g);
        if (Box(ui, "##group", a, b, lib.phoneGroup == g, ui.S(12))) lib.phoneGroup = g;
        ImGui::PopID();
        const bool on = lib.phoneGroup == g;
        ui.icons.Draw(ImGui::GetWindowDrawList(), "ui-folder", {a.x + ui.S(12), a.y + ui.S(12)}, ui.S(16), on ? c.accent : c.textSecondary);
        const std::string name = Tr(std::string(parts::Groups()[static_cast<std::size_t>(g)].name));
        BoxText(ui, {a.x + ui.S(26), a.y}, b, name.c_str(), on ? c.text : c.textSecondary, false);
        ImGui::SetCursorScreenPos({a.x, b.y});
        ImGui::Dummy({fw, ui.S(6)});
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    {
        const ImVec2 a{lx, bottom - addH}, b{lx + lw, bottom};
        if (Box(ui, "##phone-lib-add", a, b, false, ui.S(12))) ImGui::OpenPopup("##phone-lib-add-menu");
        BoxText(ui, a, b, Tr("+ Пакет / моя деталь"), c.textSecondary, true, ui.fonts.body, ui.S(12.5f));
        ImGui::SetNextWindowPos({a.x, a.y}, ImGuiCond_Always, {0, 1});
        if (ImGui::BeginPopup("##phone-lib-add-menu")) {
            if (ImGui::Selectable(Tr("Добавить пакет стандартных изделий…"), false, 0, {0, ui.S(40)})) InstallStandardPack(app);
            const bool partOpen = app.Doc() && !app.workspace.ShowsHome() && !InAssembly(app);
            if (ImGui::Selectable(Tr("Моя деталь — в библиотеку…"), false, partOpen ? 0 : ImGuiSelectableFlags_Disabled, {0, ui.S(40)})) {
                close = true;
                OpenSaveToLibrary(app);
            }
            ImGui::EndPopup();
        }
    }

    // --- Middle: the varieties of the folder and their standards ---
    ImGui::SetCursorScreenPos({x1 + ui.S(10), top});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ui.S(3));
    ImGui::BeginChild("##phone-lib-standards", {midW - ui.S(20), bottom - top}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (lib.phoneGroup >= 0 && lib.phoneGroup < static_cast<int>(parts::Groups().size())) {
        const float w = ImGui::GetContentRegionAvail().x - ui.S(4);
        for (parts::PartKind k : parts::Groups()[static_cast<std::size_t>(lib.phoneGroup)].kinds) {
            std::vector<const parts::StandardInfo*> items;
            for (const parts::StandardInfo* s : parts::StandardsOf(k))
                if (static_cast<int>(parts::FamilyOf(*s)) == lib.family || parts::InEveryFamily(*s)) items.push_back(s);
            if (items.empty()) continue;
            const ImVec2 h = ImGui::GetCursorScreenPos();
            std::string variety = Tr(std::string(parts::KindOf(k).variety));
            ImGui::PushClipRect(h, {h.x + w, h.y + ui.S(24)}, true);
            ImGui::GetWindowDrawList()->AddText(ui.fonts.strong, ui.S(11), {h.x + ui.S(4), h.y + ui.S(6)}, ui::ToU32(c.textMuted),
                                                variety.c_str());
            ImGui::PopClipRect();
            ImGui::Dummy({w, ui.S(24)});
            for (const parts::StandardInfo* s : items) {
                const ImVec2 a = ImGui::GetCursorScreenPos(), b{a.x + w, a.y + ui.S(50)};
                const bool on = lib.chosen.standard == s->id;
                ImGui::PushID(s->id.data(), s->id.data() + s->id.size());
                if (Box(ui, "##standard", a, b, on, ui.S(12))) PickLibraryPart(app, LibraryPickFor(*s, lib.chosen));
                ImGui::PopID();
                const std::string title(s->title);
                ImDrawList* cdl = ImGui::GetWindowDrawList();
                ImGui::PushClipRect(a, b, true);
                cdl->AddText(ui.fonts.medium, ui.S(13), {a.x + ui.S(12), a.y + (s->note.empty() ? ui.S(17) : ui.S(9))}, ui::ToU32(c.text),
                             title.c_str());
                if (!s->note.empty())
                    cdl->AddText(ui.fonts.body, ui.S(11), {a.x + ui.S(12), a.y + ui.S(28)}, ui::ToU32(c.textMuted),
                                 Tr(std::string(s->note)).c_str());
                ImGui::PopClipRect();
                ImGui::SetCursorScreenPos({a.x, b.y});
                ImGui::Dummy({w, ui.S(6)});
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    // --- Right: the part ---
    const float rx = x2 + ui.S(16), rw = pos.x + size.x - rx - ui.S(16);
    if (CloseCross(ui, "##phone-lib-close", {pos.x + size.x - ui.S(48), top})) close = true;
    const parts::StandardInfo* standard = parts::FindStandard(lib.chosen.standard);
    if (!standard) {
        const char* hint = Tr("Выберите папку и стандарт");
        BoxText(ui, {rx, top}, {rx + rw, bottom}, hint, c.textMuted, true, ui.fonts.body);
    } else {
        const parts::KindInfo& kind = parts::KindOf(standard->kind);
        const std::string title = Trf("{} {}", Tr(std::string(kind.title)), standard->title);
        ImGui::SetCursorScreenPos({rx, top});
        ImGui::PushTextWrapPos(rx + rw - ui.S(48) - ImGui::GetWindowPos().x);
        {
            ui::FontScope font(ui.fonts.title);
            ImGui::TextUnformatted(title.c_str());
        }
        ImGui::PopTextWrapPos();
        float ry = std::max(ImGui::GetCursorScreenPos().y, top + ui.S(40)) + ui.S(6);

        // Sizes: the caption and the search on one line, the tiles under them.
        const std::vector<std::string> sizes = parts::Sizes(*standard);
        if (std::ranges::find(sizes, lib.chosen.size) == sizes.end() && !sizes.empty()) lib.chosen.size = sizes.front();
        const std::string_view st = parts::SizeTitle(*standard);
        const std::string caption = !st.empty() ? Tr(st) : parts::HasThread(standard->kind) ? Tr("Резьба") : Tr("Размер");
        Caption(ui, {rx, ry + ui.S(10)}, caption.c_str());
        const float searchW = std::min(ui.S(180), rw * 0.45f);
        if (sizes.size() > 8) WordsField(app, "##phone-size-search", Tr("Найти размер"), lib.sizeFilter, {rx + rw - searchW, ry}, searchW);
        ry += ui.S(42);
        std::string want;   // "40x40" finds 40×40×…
        for (const char ch : lib.sizeFilter)
            want += ch == 'x' || ch == 'X' || ch == '*' ? std::string("\xC3\x97") : ch == ',' ? std::string(".") : std::string(1, ch);
        const float buttonH = ui.S(46), designH = ui.S(22);
        const bool free = parts::FreeLength(*standard).has_value();
        const std::vector<double> lengths = parts::Lengths(*standard, lib.chosen.size);
        const std::vector<parts::InputInfo> inputs = parts::Inputs(*standard);
        const float fieldsH = (free || !lengths.empty() ? ui.S(64) : 0.0f) + static_cast<float>(inputs.size()) * ui.S(64);
        const float tilesH = std::max(ui.S(80), bottom - buttonH - designH - ui.S(16) - fieldsH - ry);
        ImGui::SetCursorScreenPos({rx, ry});
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ui.S(3));
        ImGui::BeginChild("##phone-lib-sizes", {rw, tilesH}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        {
            const int columns = rw > ui.S(380) ? 4 : 3;
            const float gap = ui.S(6), inner = rw - ui.S(6), tw = (inner - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
            const ImVec2 o = ImGui::GetCursorScreenPos();
            int n = 0;
            for (const std::string& sz : sizes) {
                if (!want.empty() && sz.find(want) == std::string::npos) continue;
                const ImVec2 a{o.x + static_cast<float>(n % columns) * (tw + gap), o.y + static_cast<float>(n / columns) * (ui.S(36) + gap)};
                const ImVec2 b{a.x + tw, a.y + ui.S(36)};
                const bool on = sz == lib.chosen.size;
                ImGui::PushID(n);
                if (Box(ui, "##size", a, b, on, ui.S(9))) lib.chosen.size = sz;
                ImGui::PopID();
                BoxText(ui, a, b, sz.c_str(), on ? c.accent : c.text, true, ui.fonts.medium, ui.S(12.5f));
                ++n;
            }
            const int rows = (n + columns - 1) / columns;
            ImGui::SetCursorScreenPos(o);
            ImGui::Dummy({inner, static_cast<float>(rows) * (ui.S(36) + gap)});
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ry += tilesH + ui.S(4);

        // The length (and typed sizes): fields on the numpad.
        const auto valueField = [&](const char* id, const std::string& label, double& value, double lo, double hi) {
            Caption(ui, {rx, ry}, label.c_str());
            ImGui::SetCursorScreenPos({rx, ry + ui.S(18)});
            if (ui::CommitInput(ui, id, std::format("{:g}", value), app.shell.edit, rw, true)) {
                const Expected<double> typed = EvaluateExpression(app.shell.edit.text, Quantity::Length, UnitSystem{}, {});
                if (typed && *typed >= lo && *typed <= hi) value = *typed;
                else app.notes.Error(Trf("Нужно число от {:g} до {:g} мм", lo, hi));
            }
            ry += ui.S(64);
        };
        if (const auto range = parts::FreeLength(*standard)) {
            if (lib.chosen.length < range->min || lib.chosen.length > range->max) lib.chosen.length = range->preset;
            valueField("##phone-length", Trf("Длина, мм ({:g}–{:g})", range->min, range->max), lib.chosen.length, range->min, range->max);
        } else if (!lengths.empty()) {
            if (std::ranges::none_of(lengths, [&](double l) { return std::abs(l - lib.chosen.length) < 1e-9; }))
                lib.chosen.length = lengths[lengths.size() / 2];
            // A list of lengths: the tiles' way, one row that scrolls sideways.
            Caption(ui, {rx, ry}, Tr("Длина, мм"));
            ImGui::SetCursorScreenPos({rx, ry + ui.S(18)});
            ImGui::BeginChild("##phone-lengths", {rw, ui.S(40)}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar);
            const ImVec2 o = ImGui::GetCursorScreenPos();
            float x = o.x;
            for (std::size_t i = 0; i < lengths.size(); ++i) {
                const std::string t = std::format("{:g}", lengths[i]);
                const ImVec2 a{x, o.y}, b{x + ui.S(56), o.y + ui.S(34)};
                const bool on = std::abs(lengths[i] - lib.chosen.length) < 1e-9;
                ImGui::PushID(static_cast<int>(i));
                if (Box(ui, "##len", a, b, on, ui.S(9))) lib.chosen.length = lengths[i];
                ImGui::PopID();
                BoxText(ui, a, b, t.c_str(), on ? c.accent : c.text, true);
                x += ui.S(62);
            }
            ImGui::SetCursorScreenPos(o);
            ImGui::Dummy({x - o.x, ui.S(34)});
            ImGui::EndChild();
            ry += ui.S(64);
        } else {
            lib.chosen.length = 0;
        }
        for (const parts::InputInfo& in : inputs) {
            auto it = std::ranges::find(lib.chosen.inputs, std::string(in.name), &std::pair<std::string, double>::first);
            if (it == lib.chosen.inputs.end()) {
                lib.chosen.inputs.emplace_back(std::string(in.name), in.preset);
                it = lib.chosen.inputs.end() - 1;
            }
            ImGui::PushID(in.name.data(), in.name.data() + in.name.size());
            valueField("##phone-input", Trf("{} ({:g}–{:g})", Tr(in.title), in.min, in.max), it->second, in.min, in.max);
            ImGui::PopID();
        }

        // The designation, then one wide button: a new part, or into the assembly.
        const parts::Request request{lib.chosen.standard, lib.chosen.size, lib.chosen.length, lib.realThread, lib.appearance,
                                     lib.chosen.inputs};
        const std::string designation = parts::Designation(request);
        ImGui::PushClipRect({rx, bottom - buttonH - designH - ui.S(4)}, {rx + rw, bottom - buttonH}, true);
        dl->AddText(ui.fonts.mono, ui.S(11.5f), {rx, bottom - buttonH - designH}, ui::ToU32(c.textSecondary), designation.c_str());
        ImGui::PopClipRect();
        const bool intoAssembly = InAssembly(app);
        if (parts::IsUserStandard(standard->id)) {   // the user's own item: it can go (two taps)
            const bool asked = lib.confirmRemove == standard->id;
            const float bw = ui.S(48);
            const ImVec2 a{rx + rw - bw, bottom - buttonH}, b{rx + rw, bottom};
            if (Box(ui, "##phone-lib-remove", a, b, asked, ui.S(12))) {
                if (!asked) {
                    lib.confirmRemove = std::string(standard->id);
                    app.notes.Info(Tr("Нажмите ещё раз — изделие удалится из моей библиотеки"));
                } else {
                    const std::string id(standard->id);
                    lib.confirmRemove.clear();
                    if (const Status ok = parts::RemoveUserItem(StandardPackDir() / std::string(parts::kUserPackFile), id); !ok)
                        app.notes.Error(Trf("Не удалось удалить: {}", ok.GetError().message));
                    else {
                        LoadStandardPacks(app);
                        lib.chosen = {};
                        std::erase_if(lib.recent, [&](const auto& r) { return r.standard == id; });
                        app.notes.Success(Tr("Изделие удалено из моей библиотеки"));
                    }
                    ImGui::End();
                    return;   // the library was rebuilt: nothing below may use `standard`
                }
            }
            ui.icons.Draw(dl, "ui-trash", {a.x + (bw - ui.S(18)) / 2, a.y + (buttonH - ui.S(18)) / 2}, ui.S(18), c.danger);
            if (WideButton(ui, "##phone-lib-go", "ui-check", intoAssembly ? Tr("Вставить в сборку") : Tr("Создать деталь"),
                           {rx, bottom - buttonH}, {rx + rw - bw - ui.S(8), bottom}) &&
                (intoAssembly ? PlaceStandardPart(app, request) : CreateStandardPart(app, request)))
                close = true;
        } else if (WideButton(ui, "##phone-lib-go", "ui-check", intoAssembly ? Tr("Вставить в сборку") : Tr("Создать деталь"),
                              {rx, bottom - buttonH}, {rx + rw, bottom}) &&
                   (intoAssembly ? PlaceStandardPart(app, request) : CreateStandardPart(app, request))) {
            close = true;
        }
        NumpadOver(app, {x2 + 1, top}, {pos.x + size.x, pos.y + size.y - ms.insets.bottom});
    }
    if (close) app.shell.showLibrary = false;
    ImGui::End();
}

// --- 6b: «В мою библиотеку» ---------------------------------------------------------

void DrawPhoneSaveToLibrary(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    auto& st = app.shell.saveToLibrary;
    const mobile::MobileState& ms = app.shell.mobile;
    ImVec2 pos, size;
    if (!BeginFullScreen(app, "##phone-save-library", pos, size)) {
        ImGui::End();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float top = pos.y + std::max(ui.S(12), ms.insets.top), bottom = pos.y + size.y - std::max(ui.S(12), ms.insets.bottom);
    const float leftW = std::clamp(size.x * 0.36f, ui.S(220), ui.S(300)), x1 = pos.x + leftW;
    dl->AddRectFilled(pos, {x1, pos.y + size.y}, ui::ToU32(c.ribbon));
    dl->AddLine({x1, pos.y}, {x1, pos.y + size.y}, ui::ToU32(c.divider));
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);

    // --- Left: the name, the folder, the variables ---
    const float lx = pos.x + ui.S(12), lw = leftW - ui.S(24);
    dl->AddText(ui.fonts.title, ui.S(17), {lx + ui.S(2), top}, ui::ToU32(c.text), Tr("В мою библиотеку"));
    float ly = top + ui.S(36);
    Caption(ui, {lx, ly}, Tr("Название изделия"));
    WordsField(app, "##phone-save-title", "", st.title, {lx, ly + ui.S(18)}, lw);
    ly += ui.S(66);
    Caption(ui, {lx, ly}, Tr("Папка"));
    WordsField(app, "##phone-save-group", "", st.group, {lx, ly + ui.S(18)}, lw);
    ly += ui.S(66);
    Caption(ui, {lx, ly}, Tr("Переменные → столбцы (L — длина, любая)"));
    ly += ui.S(20);
    {
        float px = lx;
        for (std::size_t i = 0; i < st.variables.size(); ++i) {
            const float w = ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, st.variables[i].c_str()).x + ui.S(46);
            if (px > lx && px + w > lx + lw) px = lx, ly += ui.S(40);
            const ImVec2 a{px, ly}, b{px + w, ly + ui.S(34)};
            const bool on = st.use[i] != 0;
            ImGui::PushID(static_cast<int>(i));
            if (Box(ui, "##var", a, b, on, ui.S(17))) st.use[i] = on ? 0 : 1;
            ImGui::PopID();
            if (on) ui.icons.Draw(dl, "ui-check", {a.x + ui.S(12), a.y + ui.S(10)}, ui.S(14), c.accent);
            dl->AddText(ui.fonts.medium, ui.S(13), {a.x + ui.S(30), a.y + ui.S(9)}, ui::ToU32(on ? c.accent : c.text), st.variables[i].c_str());
            px += w + ui.S(6);
        }
    }

    // --- Right: the table of sizes, the save button ---
    const float rx = x1 + ui.S(16), rw = pos.x + size.x - rx - ui.S(16);
    if (CloseCross(ui, "##phone-save-close", {pos.x + size.x - ui.S(48), top})) close = true;
    dl->AddText(ui.fonts.title, ui.S(15), {rx, top + ui.S(2)}, ui::ToU32(c.text), Tr("Размеры"));
    Caption(ui, {rx, top + ui.S(28)}, Tr("Первая строка — деталь как есть"));
    std::vector<std::size_t> shown;
    for (std::size_t i = 0; i < st.variables.size(); ++i)
        if (st.use[i] && st.variables[i] != "L") shown.push_back(i);
    const float buttonH = ui.S(46), tableTop = top + ui.S(52);
    ImGui::SetCursorScreenPos({rx, tableTop});
    ImGui::BeginChild("##phone-save-rows", {rw, bottom - buttonH - ui.S(10) - tableTop}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    {
        const float xw = ui.S(34), gap = ui.S(6);
        const float cell = shown.empty() ? 0.0f : (rw - xw - gap * static_cast<float>(shown.size())) / static_cast<float>(shown.size());
        const ImVec2 o = ImGui::GetCursorScreenPos();
        for (std::size_t k = 0; k < shown.size(); ++k)
            ImGui::GetWindowDrawList()->AddText(ui.fonts.body, ui.S(11.5f), {o.x + static_cast<float>(k) * (cell + gap) + ui.S(4), o.y},
                                                ui::ToU32(c.textMuted), st.variables[shown[k]].c_str());
        float y = o.y + ui.S(18);
        std::size_t removeRow = st.cells.size();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ui.S(12, 9));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui.S(10));
        for (std::size_t r = 0; r < st.cells.size(); ++r) {
            ImGui::PushID(static_cast<int>(r));
            for (std::size_t k = 0; k < shown.size(); ++k) {
                ImGui::SetCursorScreenPos({o.x + static_cast<float>(k) * (cell + gap), y});
                ImGui::SetNextItemWidth(cell);
                ImGui::InputText(std::format("##c{}", shown[k]).c_str(), &st.cells[r][shown[k]], ImGuiInputTextFlags_CharsScientific);
            }
            if (st.cells.size() > 1) {
                ImGui::SetCursorScreenPos({o.x + rw - xw, y});
                if (ui::IconButton(ui, "##del", "ui-close", xw, ui.S(14), Tr("Убрать размер"))) removeRow = r;
            }
            ImGui::PopID();
            y += ui.S(42);
        }
        ImGui::PopStyleVar(2);
        if (removeRow < st.cells.size()) st.cells.erase(st.cells.begin() + static_cast<std::ptrdiff_t>(removeRow));
        const ImVec2 a{o.x, y + ui.S(2)}, b{o.x + rw, y + ui.S(38)};
        if (Box(ui, "##add-row", a, b, false, ui.S(10)) && !st.cells.empty()) st.cells.push_back(st.cells.back());
        BoxText(ui, a, b, Tr("+ Добавить размер"), c.accent, true);
        ImGui::SetCursorScreenPos(o);
        ImGui::Dummy({rw, b.y - o.y + ui.S(8)});
    }
    ImGui::EndChild();
    // On success SaveLibraryItem closes this window and opens the library on the item.
    if (WideButton(ui, "##phone-save-go", "ui-check", Tr("Сохранить в библиотеку"), {rx, bottom - buttonH}, {rx + rw, bottom})) SaveLibraryItem(app);
    NumpadOver(app, {x1 + 1, top}, {pos.x + size.x, pos.y + size.y - ms.insets.bottom});
    if (close) st.open = false;
    ImGui::End();
}

} // namespace bz::app
