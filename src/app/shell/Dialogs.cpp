// Windows on top of the work: "save changes?", "МЦХ" and "about". All close
// with Esc.

#if defined(__ANDROID__)
#include "platform/Android.h"
#endif
#include "app/BetaTesting.h"
#include "app/Format.h"
#include "app/Language.h"
#include "app/Materials.h"
#include "app/Operations.h"
#include "app/ShortcutPresets.h"
#include "app/Backgrounds.h"
#include "app/shell/Shell.h"
#include "app/shell/TouchpadSettings.h"
#include "app/mobile/MobileLayout.h"
#include "core/Expression.h"
#include "core/I18n.h"
#include "core/Version.h"
#include "document/Commands.h"
#include "app/assembly/AssemblyUi.h"
#include "document/StandardPacks.h"
#include "document/StandardParts.h"
#include "core/PathText.h"
#include "modules/ModuleManager.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Menus.h"
#include "platform/Launch.h"
#include "ui/Widgets.h"

#include <array>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>

namespace bz::app {

bool BeginDialog(ui::Ui& ui, const char* id, bool& open, float width) {
    if (open && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
#if defined(__ANDROID__)
    // A phone held sideways is ~400 dp tall: the dialog fits the screen and
    // scrolls (a swipe) instead of running off it.
    width = std::min(width, display.x - ui.S(24));
    ImGui::SetNextWindowSizeConstraints({width, 0}, {width, display.y - ui.S(16)});
#endif
    ImGui::SetNextWindowSize({width, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(24, 22));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ui.S(8, 8));
    const bool visible = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    if (!visible) open = false;
    return visible;
}

namespace {

/// Label on the left, value on the right in the number font.
void ValueRow(ui::Ui& ui, const char* label, const std::string& value) {
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(26);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ui.fonts.body, ui.S(13), {p.x, p.y + ui.S(5)}, ui::ToU32(ui.C().textSecondary), label);
    const ImVec2 vs = ui.fonts.mono->CalcTextSizeA(ui.S(13), FLT_MAX, 0, value.c_str());
    dl->AddText(ui.fonts.mono, ui.S(13), {p.x + width - vs.x, p.y + ui.S(5)}, ui::ToU32(ui.C().text), value.c_str());
    ImGui::Dummy({width, h});
}

/// "Фон": the view's background, from the presets or a picture of one's own.
void DrawBackgroundMenu(AppContext& app) {
    ui::Ui& ui = app.ui;
    if (app.shell.backgroundMenu) {
        ImGui::OpenPopup("##backgrounds");
        app.shell.backgroundMenu = false;
    }
    ui::MenuStyle style(ui);   // before Begin: must outlive the End call
    if (!ImGui::BeginPopup("##backgrounds")) return;
    const ui::ThemeColors& c = ui.C();
    for (const BackgroundPreset& preset : BackgroundPresets()) {
        const bool current = app.settings.background == preset.id;
        // A small sample before the name.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ui::MenuItem(ui, ("          " + std::string(Tr(preset.name))).c_str(), nullptr, {}, current)) app.settings.background = preset.id;
        const BackgroundColours bg = ColoursOf(preset, c);
        const float h = ImGui::GetItemRectSize().y, s = ui.S(16);
        const ImVec2 a{p.x + ui.S(34), p.y + (h - s) / 2}, b{a.x + s, a.y + s};
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(a, b, ui::ToU32(bg.top), ui::ToU32(bg.top), ui::ToU32(bg.bottom), ui::ToU32(bg.bottom));
        ImGui::GetWindowDrawList()->AddRect(a, b, ui::ToU32(c.fieldBorder), ui.S(3));
    }
    ImGui::Separator();
    const bool picture = app.settings.background == kPictureBackground;
    if (ui::MenuItem(ui, Tr("Своя картинка…"), nullptr, "ui-open", picture)) {
        if (const auto file = platform::ShowOpenDialog(Tr("Картинка для фона"), {{Tr("Картинки"), "*.png *.jpg *.jpeg *.bmp *.tga"}},
                                                       platform::DocumentsDir())) {
            app.settings.backgroundPicture = ToUtf8(*file);
            app.settings.background = std::string(kPictureBackground);
        }
    }
    ImGui::EndPopup();
}

/// The "МЦХ" window: mass from the part's material, volume, area, centre of
/// mass and overall size. Picking a material here sets the part's material.
void DrawMassProperties(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    Session* s = app.Doc();
    if (!s) app.shell.showMassProperties = false;
    if (!BeginDialog(ui, "##mass", app.shell.showMassProperties, ui.S(460))) return;
    const auto close = [&] {
        app.shell.showMassProperties = false;
        ImGui::CloseCurrentPopup();
    };
    const std::optional<kernel::MassProperties> props = s ? s->model.Properties() : std::nullopt;
    if (!props) {
        close();
        ImGui::EndPopup();
        return;
    }
    const UnitSystem& units = s->doc.units;
    const std::string unit = UnitLabel(Quantity::Length, units);
    const double k = units.ToDisplay(1.0, Quantity::Length);   // display units per mm

    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(Tr("Массово-центровочные характеристики"));
    }
    ImGui::Dummy({0, ui.S(4)});

    // Material: one of the known ones gives the density.
    ui::FieldLabel(ui, Tr("Материал"));
    const auto materials = KnownMaterials();
    std::vector<const char*> names;
    int current = -1;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        names.push_back(materials[i].name.data());
        if (materials[i].name == s->doc.info.material) current = static_cast<int>(i);
    }
    const int chosen = ui::Segmented(ui, "material", names, current, 3, ImGui::GetContentRegionAvail().x);
    if (chosen != current && chosen >= 0) {
        doc::PartInfo info = s->doc.info;
        info.material = std::string(materials[static_cast<std::size_t>(chosen)].name);
        s->Execute(doc::SetPartInfo(std::move(info)));
    }
    ImGui::Dummy({0, ui.S(6)});

    const Material* material = FindMaterial(s->doc.info.material);
    std::string mass = "\xE2\x80\x94";   // em dash: no density known
    if (material) {
        const double grams = props->volume / 1000.0 * material->density;   // mm^3 -> cm^3
        mass = grams >= 1000.0 ? Trf("{} кг", FormatGrouped(grams / 1000.0, 3)) : Trf("{} г", FormatGrouped(grams, 2));
    }
    ValueRow(ui, Tr("Масса"), mass);
    if (!material) {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::TextUnformatted(s->doc.info.material.empty() ? Tr("Выберите материал, чтобы узнать массу.")
                                                            : Tr("Плотность этого материала неизвестна — выберите из списка."));
        ImGui::PopStyleColor();
    }
    ui::Divider(ui, c.dividerSoft);
    ValueRow(ui, Tr("Объём"), FormatGrouped(props->volume * k * k * k, 2) + " " + unit + "\xC2\xB3");
    ValueRow(ui, Tr("Площадь поверхности"), FormatGrouped(props->area * k * k, 2) + " " + unit + "\xC2\xB2");
    ui::Divider(ui, c.dividerSoft);
    const auto length = [&](double mm) { return FormatValue(mm, Quantity::Length, units, 3); };
    ValueRow(ui, Tr("Центр масс X"), length(props->centre.x) + " " + unit);
    ValueRow(ui, Tr("Центр масс Y"), length(props->centre.y) + " " + unit);
    ValueRow(ui, Tr("Центр масс Z"), length(props->centre.z) + " " + unit);
    ui::Divider(ui, c.dividerSoft);
    const Vec3 size = props->bounds.max - props->bounds.min;
    ValueRow(ui, Tr("Габариты X \xC3\x97 Y \xC3\x97 Z"),
             length(size.x) + " \xC3\x97 " + length(size.y) + " \xC3\x97 " + length(size.z) + " " + unit);

    ImGui::Dummy({0, ui.S(8)});
    const float bw = (ImGui::GetContentRegionAvail().x - ui.S(8)) / 2;
    if (ui::SecondaryButton(ui, "##mass-copy", Tr("Копировать"), nullptr, {bw, ui.S(36)})) {
        const std::string text = Trf(
            "Материал: {}\nМасса: {}\nОбъём: {} {}3\nПлощадь: {} {}2\nЦентр масс: {}; {}; {} {}\nГабариты: {} x {} x {} {}\n",
            material ? std::string(Tr(material->name)) : std::string(Tr("не задан")), mass, FormatGrouped(props->volume * k * k * k, 2), unit,
            FormatGrouped(props->area * k * k, 2), unit, length(props->centre.x), length(props->centre.y),
            length(props->centre.z), unit, length(size.x), length(size.y), length(size.z), unit);
        ImGui::SetClipboardText(text.c_str());
        app.notes.Info(Tr("Характеристики скопированы"));
    }
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##mass-close", Tr("Закрыть"), "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
    ImGui::EndPopup();
}

// --- "Библиотека": the standard parts window -----------------------------------

namespace parts = doc::parts;

/// Up to this many sizes are buttons; a longer table is a list.
constexpr std::size_t kLibrarySizeButtons = 48;

/// Chooses a standard part (and remembers it among the recent ones).
void PickPart(AppContext& app, const ShellState::Library::Pick& pick) {
    auto& lib = app.shell.library;
    const parts::StandardInfo* before = parts::FindStandard(lib.chosen.standard);
    const parts::StandardInfo* s = parts::FindStandard(pick.standard);
    if (!s) return;
    if (!before || before->kind != s->kind) lib.appearance.clear();   // another kind: its own default look
    lib.chosen = pick;
    lib.family = static_cast<int>(parts::FamilyOf(*s));
    // Show where it is in the tree.
    for (int g = 0; g < static_cast<int>(parts::Groups().size()); ++g)
        for (parts::PartKind k : parts::Groups()[static_cast<std::size_t>(g)].kinds)
            if (k == s->kind) lib.openGroups.insert(g);
    lib.openKinds.insert(static_cast<int>(s->kind));
    std::erase_if(lib.recent, [&](const auto& r) { return r.standard == pick.standard; });
    lib.recent.insert(lib.recent.begin(), pick);
    if (lib.recent.size() > 8) lib.recent.resize(8);
}

/// The pick for a standard keeping the current size and length where it has them.
ShellState::Library::Pick PickFor(const parts::StandardInfo& s, const ShellState::Library::Pick& from) {
    ShellState::Library::Pick p{std::string(s.id), {}, 0, {}};
    for (const parts::InputInfo& in : parts::Inputs(s)) {   // typed sizes: keep a value that still fits
        double v = in.preset;
        for (const auto& [name, value] : from.inputs)
            if (name == in.name && value >= in.min && value <= in.max) v = value;
        p.inputs.emplace_back(std::string(in.name), v);
    }
    const parts::StandardInfo* old = parts::FindStandard(from.standard);
    p.size = old ? parts::MatchingSize(*old, from.size, s) : parts::Sizes(s).front();
    if (const std::optional<parts::LengthRange> range = parts::FreeLength(s)) {
        // Any length: keep the one there was if it was a cut length too (a bolt's 38 mm is no length for a tube).
        const bool wasFree = old && parts::FreeLength(*old);
        p.length = wasFree && from.length >= range->min && from.length <= range->max ? from.length : range->preset;
        return p;
    }
    const std::vector<double> lengths = parts::Lengths(s, p.size);
    if (!lengths.empty()) {
        p.length = lengths[lengths.size() / 2];
        for (double l : lengths)
            if (std::abs(l - from.length) < 1e-9) p.length = l;
    }
    return p;
}

/// A row of the tree: indent, optional chevron and icon, text. True when clicked.
bool TreeRow(ui::Ui& ui, const char* id, int indent, const char* chevron, const char* icon, const std::string& text,
             const char* note, bool selected, bool muted, float width) {
    const ui::ThemeColors& c = ui.C();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(28);
    const bool clicked = ImGui::InvisibleButton(id, {width, h});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected || hovered) dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(selected ? c.accentSoft : c.hover), ui.S(6));
    float x = p.x + ui.S(6) + ui.S(16) * static_cast<float>(indent);
    if (chevron) ui.icons.Draw(dl, chevron, {x, p.y + (h - ui.S(12)) / 2}, ui.S(12), c.textMuted);
    x += ui.S(16);
    if (icon) {
        ui.icons.Draw(dl, icon, {x, p.y + (h - ui.S(15)) / 2}, ui.S(15), c.textSecondary);
        x += ui.S(21);
    }
    const ImVec4 clip{p.x, p.y, p.x + width - ui.S(4), p.y + h};
    ImFont* font = selected ? ui.fonts.medium : ui.fonts.body;
    dl->AddText(font, ui.S(13), {x, p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(muted ? c.textDisabled : c.text), text.c_str(),
                nullptr, 0, &clip);
    if (note && *note) {
        const float tw = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, text.c_str()).x;
        dl->AddText(ui.fonts.caption, ui.S(11), {x + tw + ui.S(6), p.y + (h - ui.S(11)) / 2}, ui::ToU32(c.textMuted), note, nullptr, 0,
                    &clip);
    }
    return clicked;
}

/// Section caption in the main part of the window.
void LibraryHeading(ui::Ui& ui, const char* text) {
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, text);
}

/// Wrapped muted text.
void LibraryNote(ui::Ui& ui, const char* text) {
    ui::FontScope font(ui.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textMuted));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

/// The chosen part: name, parameters, look, analogues, thread.
void DrawLibraryPart(AppContext& app, const parts::StandardInfo& standard, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    auto& lib = app.shell.library;
    const parts::KindInfo& kind = parts::KindOf(standard.kind);
    {
        ui::FontScope font(ui.fonts.display);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(Trf("{} {}", Tr(kind.title.data()), standard.title).c_str());
        ImGui::PopTextWrapPos();
    }
    if (!standard.note.empty()) LibraryNote(ui, Tr(standard.note.data()));

    // Parameters.
    const std::vector<std::string> sizes = parts::Sizes(standard);
    if (std::ranges::find(sizes, lib.chosen.size) == sizes.end()) lib.chosen.size = sizes.front();
    const std::string_view sizeTitle = parts::SizeTitle(standard);   // a pack names its sizes itself
    const std::string heading = !sizeTitle.empty()                                   ? Tr(sizeTitle)
                                : standard.kind == parts::PartKind::BallBearing    ? Tr("Подшипник")
                                : standard.kind == parts::PartKind::RingExternal ? Tr("Диаметр вала, мм")
                                : standard.kind == parts::PartKind::RingInternal ? Tr("Диаметр отверстия, мм")
                                : parts::HasThread(standard.kind)                ? Tr("Резьба")
                                                                                 : Tr("Диаметр, мм");
    LibraryHeading(ui, heading.c_str());
    if (sizes.size() <= kLibrarySizeButtons) {
        std::vector<const char*> labels;
        for (const std::string& s : sizes) labels.push_back(s.c_str());
        const int current = static_cast<int>(std::ranges::find(sizes, lib.chosen.size) - sizes.begin());
        if (const int chosen = ui::Segmented(ui, "size", labels, current, 6, width); chosen != current && chosen >= 0)
            lib.chosen.size = sizes[static_cast<std::size_t>(chosen)];
    } else {   // a long table (profiles): a list instead of a wall of buttons
        ImGui::SetNextItemWidth(width);
        lib.sizeListOpen = ImGui::BeginCombo("##size-list", lib.chosen.size.c_str(), ImGuiComboFlags_HeightLarge);
        if (lib.sizeListOpen) {
            // A filter over the list: "57" or "57x4" finds 57×4 among a thousand tubes.
            if (ImGui::IsWindowAppearing()) lib.sizeFilter.clear();
            // Typing goes into the filter at once: the field takes the focus a
            // frame later, so the keys of this frame are added by hand.
            if (ImGui::IsWindowAppearing()) lib.sizeFilterActive = false;
            if (!lib.sizeFilterActive) {
                ImGuiIO& io = ImGui::GetIO();
                for (const ImWchar ch : io.InputQueueCharacters) {   // UTF-8 by hand: Cyrillic letters of a size too
                    const auto u = static_cast<unsigned>(ch);
                    if (u < 0x80) lib.sizeFilter += static_cast<char>(u);
                    else if (u < 0x800) lib.sizeFilter += {static_cast<char>(0xC0 | (u >> 6)), static_cast<char>(0x80 | (u & 0x3F))};
                    else
                        lib.sizeFilter += {static_cast<char>(0xE0 | (u >> 12)), static_cast<char>(0x80 | ((u >> 6) & 0x3F)),
                                           static_cast<char>(0x80 | (u & 0x3F))};
                }
                io.InputQueueCharacters.resize(0);
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##size-filter", Tr("Найти размер"), &lib.sizeFilter);
            lib.sizeFilterActive = ImGui::IsItemActive();
            std::string want;
            for (const char ch : lib.sizeFilter) want += ch == 'x' || ch == 'X' || ch == '*' ? std::string("\xC3\x97") : ch == ',' ? std::string(".") : std::string(1, ch);
            for (const std::string& size : sizes) {
                if (!want.empty() && size.find(want) == std::string::npos) continue;
                const bool on = size == lib.chosen.size;
                if (ImGui::Selectable(size.c_str(), on)) lib.chosen.size = size;
                if (on && want.empty() && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            }
            ImGui::EndCombo();
        }
    }
    const std::vector<double> lengths = parts::Lengths(standard, lib.chosen.size);
    if (const std::optional<parts::LengthRange> range = parts::FreeLength(standard)) {
        // Any length of the range: a profile cut to size.
        LibraryHeading(ui, Trf("Длина, мм (от {:g} до {:g})", range->min, range->max).c_str());
        if (lib.chosen.length < range->min || lib.chosen.length > range->max) lib.chosen.length = range->preset;
        if (ui::CommitInput(ui, "##length-free", std::format("{:g}", lib.chosen.length), app.shell.edit, width, true)) {
            const Expected<double> typed = EvaluateExpression(app.shell.edit.text, Quantity::Length, UnitSystem{}, {});
            if (typed && *typed >= range->min && *typed <= range->max) lib.chosen.length = *typed;
            else app.notes.Error(Trf("Длина — число от {:g} до {:g} мм", range->min, range->max));
        }
    } else if (!lengths.empty()) {
        LibraryHeading(ui, Tr("Длина, мм"));
        int current = -1;
        for (std::size_t i = 0; i < lengths.size(); ++i)
            if (std::abs(lengths[i] - lib.chosen.length) < 1e-9) current = static_cast<int>(i);
        if (current < 0) {
            current = static_cast<int>(lengths.size() / 2);
            lib.chosen.length = lengths[static_cast<std::size_t>(current)];
        }
        std::vector<std::string> texts;
        for (double l : lengths) texts.push_back(std::format("{:g}", l));
        std::vector<const char*> labels;
        for (const std::string& t : texts) labels.push_back(t.c_str());
        if (const int chosen = ui::Segmented(ui, "length", labels, current, 8, width); chosen != current && chosen >= 0)
            lib.chosen.length = lengths[static_cast<std::size_t>(chosen)];
    } else {
        lib.chosen.length = 0;
    }

    // Sizes typed within a range (a sheet's width).
    for (const parts::InputInfo& in : parts::Inputs(standard)) {
        auto it = std::ranges::find(lib.chosen.inputs, std::string(in.name), &std::pair<std::string, double>::first);
        if (it == lib.chosen.inputs.end()) {
            lib.chosen.inputs.emplace_back(std::string(in.name), in.preset);
            it = lib.chosen.inputs.end() - 1;
        }
        LibraryHeading(ui, Trf("{} (от {:g} до {:g})", Tr(in.title), in.min, in.max).c_str());
        ImGui::PushID(in.name.data(), in.name.data() + in.name.size());
        if (ui::CommitInput(ui, "##input", std::format("{:g}", it->second), app.shell.edit, width, true)) {
            const Expected<double> typed = EvaluateExpression(app.shell.edit.text, Quantity::Length, UnitSystem{}, {});
            if (typed && *typed >= in.min && *typed <= in.max) it->second = *typed;
            else app.notes.Error(Trf("{} — число от {:g} до {:g} мм", Tr(in.title), in.min, in.max));
        }
        ImGui::PopID();
    }

    // Look: the metals of the standard appearances.
    LibraryHeading(ui, Tr("Стиль отображения"));
    {
        const std::string current = lib.appearance.empty() ? std::string(parts::DefaultAppearance(standard.kind)) : lib.appearance;
        const float box = ui.S(30), gap = ui.S(6);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        std::string name;
        int n = 0;
        for (const doc::Appearance& a : doc::StandardAppearances()) {
            if (a.finish != doc::Finish::Metal) continue;
            if (n++ > 0) ImGui::SameLine(0, gap);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(a.id.c_str());
            if (ImGui::InvisibleButton("##look", {box, box})) lib.appearance = a.id;
            if (ImGui::IsItemHovered()) ui::ItemTooltip(ui, Tr(a.name.c_str()));
            ImGui::PopID();
            const bool on = a.id == current;
            dl->AddRectFilled({p.x + ui.S(3), p.y + ui.S(3)}, {p.x + box - ui.S(3), p.y + box - ui.S(3)}, ui::ToU32(a.face), ui.S(5));
            dl->AddRect({p.x + ui.S(3), p.y + ui.S(3)}, {p.x + box - ui.S(3), p.y + box - ui.S(3)}, ui::ToU32(a.edges), ui.S(5), 0,
                        ui.S(1));
            if (on) {
                dl->AddRect(p, {p.x + box, p.y + box}, ui::ToU32(c.accent), ui.S(7), 0, ui.S(2));
                name = Tr(a.name.c_str());
            }
        }
        if (current == parts::DefaultAppearance(standard.kind)) name = Trf("{} (по умолчанию)", name);
        LibraryNote(ui, name.c_str());
    }

    // Analogues: other standards of the same kind.
    const std::vector<const parts::StandardInfo*> analogues = parts::Analogues(standard);
    if (!analogues.empty()) {
        LibraryHeading(ui, Tr("Аналоги из других стандартов"));
        float x = 0;
        for (const parts::StandardInfo* a : analogues) {
            const std::string label(a->title);
            const float w = ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, label.c_str()).x + ui.S(28);
            if (x > 0 && x + w > width) x = 0;
            else if (x > 0) ImGui::SameLine(0, ui.S(6));
            x += w + ui.S(6);
            ImGui::PushID(a->id.data());
            const bool clicked = ui::SecondaryButton(ui, "##analogue", label.c_str(), nullptr, {w, ui.S(30)});
            if (ImGui::IsItemHovered() && !a->note.empty()) ui::ItemTooltip(ui, Tr(a->note.data()));
            ImGui::PopID();
            if (clicked) {
                PickPart(app, PickFor(*a, lib.chosen));
                return;   // the window shows the other standard from the next frame
            }
        }
    }

    // Thread: drawn by default.
    if (parts::HasThread(standard.kind)) {
        LibraryHeading(ui, Tr("Вид резьбы"));
        static constexpr std::array<const char*, 2> kinds{N_("Условная (нарисованная)"), N_("Настоящая (вырезанная)")};
        const int current = lib.realThread ? 1 : 0;
        if (const int chosen = ui::Segmented(ui, "thread", kinds, current, 2, width); chosen != current && chosen >= 0)
            lib.realThread = chosen == 1;
        LibraryNote(ui, lib.realThread ? Tr("Резьба вырезается в модели: тяжелее, и её грани мешают позиционировать деталь в сборке.")
                                       : Tr("Резьба рисуется на гладком цилиндре; при экспорте в STEP/STL вырезается настоящая."));
    }

    // The user's own item: it can be taken out (a second click confirms).
    if (doc::parts::IsUserStandard(standard.id)) {
        ImGui::Dummy({0, ui.S(10)});
        const bool asked = lib.confirmRemove == standard.id;
        if (ui::SecondaryButton(ui, "##user-remove", asked ? Tr("Нажмите ещё раз — удалить") : Tr("Удалить из моей библиотеки"), nullptr,
                                {ui.S(260), ui.S(32)})) {
            if (!asked) lib.confirmRemove = std::string(standard.id);
            else {
                const std::string id(standard.id);   // the library is rebuilt below: no pointers into it after this
                lib.confirmRemove.clear();
                if (const Status ok = doc::parts::RemoveUserItem(StandardPackDir() / std::string(doc::parts::kUserPackFile), id); !ok)
                    app.notes.Error(Trf("Не удалось удалить: {}", ok.GetError().message));
                else {
                    LoadStandardPacks(app);
                    lib.chosen = {};
                    std::erase_if(lib.recent, [&](const auto& r) { return r.standard == id; });
                    app.notes.Success(Tr("Изделие удалено из моей библиотеки"));
                }
            }
        }
    }
}

/// "Выбор стандартного изделия": recent parts, the standards filter, the
/// folders on the left, the chosen part on the right; makes a new part.
void DrawLibrary(AppContext& app) {
    if (app.shell.mobile.phone) {   // full screen (mockup 6a)
        if (app.shell.showLibrary) DrawPhoneLibrary(app);
        return;
    }
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    if (!BeginDialog(ui, "##library", app.shell.showLibrary, ui.S(1000))) return;
    auto& lib = app.shell.library;
    const auto close = [&] {
        app.shell.showLibrary = false;
        ImGui::CloseCurrentPopup();
    };
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(Tr("Выбор стандартного изделия"));
    }
    ImGui::Dummy({0, ui.S(2)});

    // Hotbar: home (nothing chosen) and the parts chosen in this session.
    if (ui::IconButton(ui, "##library-home", "ui-home", ui.S(30), ui.S(16), Tr("К началу: ничего не выбрано"), true, lib.chosen.standard.empty()))
        lib.chosen = {};
    for (std::size_t i = 0; i < lib.recent.size(); ++i) {
        const parts::StandardInfo* s = parts::FindStandard(lib.recent[i].standard);
        if (!s) continue;
        ImGui::SameLine(0, ui.S(6));
        const std::string label = parts::ShortName(*s);
        const bool on = lib.chosen.standard == lib.recent[i].standard;
        const float w = ui.fonts.body->CalcTextSizeA(ui.S(12), FLT_MAX, 0, label.c_str()).x + ui.S(20);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = ImGui::InvisibleButton("##recent", {w, ui.S(30)});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + w, p.y + ui.S(30)}, ui::ToU32(on ? c.accentSoft : hovered ? c.hover : c.field), ui.S(15));
        dl->AddRect(p, {p.x + w, p.y + ui.S(30)}, ui::ToU32(on ? c.accentSoftBorder : c.fieldBorder), ui.S(15));
        dl->AddText(ui.fonts.body, ui.S(12), {p.x + ui.S(10), p.y + ui.S(8)}, ui::ToU32(c.text), label.c_str());
        if (clicked) {
            const auto pick = lib.recent[i];
            PickPart(app, pick);
            break;
        }
    }
    ImGui::Dummy({0, ui.S(4)});

    const float left = ui.S(300), gap = ui.S(20), height = ui.S(500);
    // The standards filter, over the folders.
    {
        static constexpr std::array<const char*, 3> families{"ГОСТ", "ISO", "DIN"};
        if (const int chosen = ui::Segmented(ui, "family", families, lib.family, 3, left); chosen != lib.family && chosen >= 0)
            lib.family = chosen;
    }
    ImGui::Dummy({0, ui.S(2)});

    // Left: folders -> varieties -> standard parts of the chosen standards body.
    ImGui::BeginChild("##library-tree", {left, height - ui.S(40)}, ImGuiChildFlags_None);
    const float treeWidth = ImGui::GetContentRegionAvail().x;
    const auto groups = parts::Groups();
    for (int g = 0; g < static_cast<int>(groups.size()); ++g) {
        const parts::GroupInfo& group = groups[static_cast<std::size_t>(g)];
        const bool open = lib.openGroups.contains(g);
        ImGui::PushID(g);
        if (TreeRow(ui, "##group", 0, open ? "ui-chevron-down" : "ui-chevron-right", "ui-folder", Tr(group.name.data()), nullptr,
                    false, false, treeWidth)) {
            if (open) lib.openGroups.erase(g);
            else lib.openGroups.insert(g);
        }
        if (open)
            for (parts::PartKind k : group.kinds) {
                const parts::KindInfo& kind = parts::KindOf(k);
                std::vector<const parts::StandardInfo*> items;
                for (const parts::StandardInfo* s : parts::StandardsOf(k))
                    if (static_cast<int>(parts::FamilyOf(*s)) == lib.family || parts::InEveryFamily(*s)) items.push_back(s);
                const bool kindOpen = lib.openKinds.contains(static_cast<int>(k));
                ImGui::PushID(static_cast<int>(k));
                if (TreeRow(ui, "##kind", 1, kindOpen ? "ui-chevron-down" : "ui-chevron-right", nullptr, Tr(kind.variety.data()), nullptr,
                            false, items.empty(), treeWidth)) {
                    if (kindOpen) lib.openKinds.erase(static_cast<int>(k));
                    else lib.openKinds.insert(static_cast<int>(k));
                }
                if (kindOpen) {
                    if (items.empty()) {
                        static constexpr std::array<const char*, 3> none{N_("Нет изделий по ГОСТ"), N_("Нет изделий по ISO"),
                                                                         N_("Нет изделий по DIN")};
                        TreeRow(ui, "##none", 2, nullptr, nullptr, Tr(none[static_cast<std::size_t>(lib.family)]), nullptr, false, true,
                                treeWidth);
                    }
                    for (const parts::StandardInfo* s : items) {
                        ImGui::PushID(s->id.data());
                        if (TreeRow(ui, "##item", 2, nullptr, nullptr, std::string(s->title), Tr(s->note.data()),
                                    lib.chosen.standard == s->id, false, treeWidth))
                            PickPart(app, PickFor(*s, lib.chosen));
                        ImGui::PopID();
                    }
                }
                ImGui::PopID();
            }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ui::SecondaryButton(ui, "##library-pack", Tr("Добавить пакет стандартных изделий"), nullptr, {left, ui.S(34)}))
        InstallStandardPack(app);   // reloads the packs: nothing above keeps a pointer into the library
    ui::ItemTooltip(ui, Tr("Файл .bzlib — или просто перетащите его в окно"));

    // Right: the chosen part, or nothing.
    const parts::StandardInfo* standard = parts::FindStandard(lib.chosen.standard);
    ImGui::SameLine(0, gap);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (height - ui.S(40)) - ImGui::GetStyle().ItemSpacing.y);
    // ImGui closes a dropped list on Esc before this frame's widgets run: what
    // was open last frame counts too, so that Esc does not close the window.
    const bool listWasOpen = lib.sizeListOpen;
    lib.sizeListOpen = false;   // DrawLibraryPart says if it is
    ImGui::BeginChild("##library-part", {0, height}, ImGuiChildFlags_None);
    if (standard) DrawLibraryPart(app, *standard, ImGui::GetContentRegionAvail().x - ui.S(8));
    ImGui::EndChild();

    // Footer.
    ui::Divider(ui, c.dividerSoft);
    standard = parts::FindStandard(lib.chosen.standard);
    const parts::Request request{lib.chosen.standard, lib.chosen.size, lib.chosen.length, lib.realThread, lib.appearance,
                                 lib.chosen.inputs};
    if (standard) ValueRow(ui, Tr("Обозначение"), parts::Designation(request));
    else ImGui::Dummy({0, ui.S(26)});
    ImGui::Dummy({0, ui.S(4)});
    const float bw = ui.S(170);
    const bool intoAssembly = InAssembly(app);
    // AutoDrop: a screw goes onto a hole picked in the view; size and length by it.
    const bool canDrop = intoAssembly && standard && CanAutoDrop(*standard);
    const float rowY = ImGui::GetCursorPosY(), rowX = ImGui::GetCursorPosX(), rowW = ImGui::GetContentRegionAvail().x;
    if (canDrop) {
        ImGui::SetCursorPosY(rowY + ui.S(8));
        ImGui::Checkbox(std::format("{}##drop-washer", Tr("С шайбой")).c_str(), &lib.dropWasher);
        ImGui::SameLine(0, ui.S(12));
        ImGui::Checkbox(std::format("{}##drop-nut", Tr("С гайкой")).c_str(), &lib.dropNut);
        ImGui::SameLine(0, ui.S(12));
        ImGui::SetCursorPosY(rowY);
        if (ui::SecondaryButton(ui, "##library-drop", Tr("По отверстию…"), nullptr, {bw, ui.S(36)}) &&
            StartAutoDrop(app, request, lib.dropWasher, lib.dropNut))
            close();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("Щёлкните по кромке отверстия в сборке: диаметр — по отверстию, длина — по пакету деталей"));
        ImGui::SameLine(0, ui.S(8));
    }
    // A part of one's own into the library ("В библиотеку"): from a part, not an assembly.
    if (!intoAssembly && app.Doc() && !app.workspace.ShowsHome()) {
        ImGui::SetCursorPosY(rowY);
        if (ui::SecondaryButton(ui, "##library-save", Tr("Моя деталь — в библиотеку…"), nullptr, {ui.S(230), ui.S(36)})) {
            close();
            OpenSaveToLibrary(app);
        }
        ui::ItemTooltip(ui, Tr("Сохранить открытую деталь в мою библиотеку: её переменные станут таблицей размеров"));
    }
    ImGui::SetCursorPosY(rowY);
    ImGui::SetCursorPosX(rowX + rowW - 2 * bw - ui.S(8));
    if (ui::SecondaryButton(ui, "##library-cancel", Tr("Закрыть"), "Esc", {bw, ui.S(36)}) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape) && !lib.sizeListOpen && !listWasOpen))
        close();
    ImGui::SameLine(0, ui.S(8));
    // In an assembly the part goes into it (its file next to the assembly).
    if ((ui::PrimaryButton(ui, "##library-create", intoAssembly ? Tr("Вставить в сборку") : Tr("Создать деталь"), "Enter",
                           {bw, ui.S(36)}, standard != nullptr) ||
         (standard && ui::EnterPressed() && !lib.sizeListOpen && !listWasOpen)) &&
        standard && (intoAssembly ? PlaceStandardPart(app, request) : CreateStandardPart(app, request)))
        close();
    ImGui::EndPopup();
}

} // namespace

void PickLibraryPart(AppContext& app, const ShellState::Library::Pick& pick) { PickPart(app, pick); }

ShellState::Library::Pick LibraryPickFor(const doc::parts::StandardInfo& standard, const ShellState::Library::Pick& from) {
    return PickFor(standard, from);
}

/// Remembers the shortcuts that differ from the shipped ones.
void StoreShortcuts(AppContext& app) {
    app.settings.shortcuts.clear();
    for (const Action& a : app.actions.All())
        if (a.chord != a.defaultChord) app.settings.shortcuts.emplace_back(a.id, ActionRegistry::ChordText(a.chord));
}

/// Язык: as the system, Russian or an installed catalog; packs are files the
/// user downloads and puts in a folder (the program stays offline).
void DrawLanguageSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    // Reading the catalogs touches the disk: once per opening of the window
    // and after the folder was opened (a pack may have been added there).
    static std::vector<i18n::Language> languages;
    if (languages.empty() || ImGui::IsWindowAppearing()) languages = AvailableLanguages();

    ui::FieldLabel(ui, Tr("Язык"));
    std::vector<const char*> names{N_("Как в системе")};
    int current = 0;
    for (std::size_t i = 0; i < languages.size(); ++i) {
        names.push_back(languages[i].name.c_str());
        if (languages[i].code == app.settings.language) current = static_cast<int>(i) + 1;
    }
    const int columns = std::min(static_cast<int>(names.size()), 3);
    if (const int chosen = ui::Segmented(ui, "language", names, current, columns, width); chosen != current) {
        app.settings.language = chosen == 0 ? std::string() : languages[static_cast<std::size_t>(chosen) - 1].code;
        if (const std::string problem = ApplyLanguage(app.settings); !problem.empty()) app.notes.Error(Tr(problem));
    }
    if (ui::LinkButton(ui, "##more-languages", Tr("Ещё языки…")) && !platform::OpenUrl(kLanguagePacksUrl))
        app.notes.Error(Tr("Не удалось открыть браузер"));
    ImGui::SameLine(0, ui.S(16));
    if (ui::LinkButton(ui, "##language-folder", Tr("Папка языков"))) {
        if (!platform::OpenFolder(LanguagePackDir())) app.notes.Error(Tr("Не удалось открыть папку"));
        languages.clear();
    }
}

/// "Графика" → "Качество" (graphics, textures, animations) or, with
/// `performance`, "Производительность" (long rebuilds): what costs time on a
/// weak computer.
void DrawOptimizationSettings(AppContext& app, float width, bool performance) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const auto caption = [&](const std::string& text) {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy({0, ui.S(6)});
    };
    if (performance) {
        ui::FieldLabel(ui, Tr("Долгое перестроение"));
        static constexpr std::array<const char*, 2> rebuild{N_("В фоне"), N_("Сразу, окно ждёт")};
        const int r = app.settings.backgroundRebuild ? 0 : 1;
        if (const int n = ui::Segmented(ui, "rebuild", rebuild, r, 2, width); n != r) app.settings.backgroundRebuild = n == 0;
        caption(Tr("В фоне окно не замирает, пока считается тяжёлая операция: модель обновится, когда расчёт закончится. "
                   "«Сразу» — по-старому, на случай если фоновый расчёт ведёт себя странно."));
        return;
    }
    ui::FieldLabel(ui, Tr("Качество графики"));
    static constexpr std::array<const char*, 3> quality{N_("Высокое"), N_("Среднее"), N_("Слабый компьютер")};
    if (const int n = ui::Segmented(ui, "graphics", quality, app.settings.graphics, 3, width); n != app.settings.graphics)
        app.settings.graphics = n;
    caption(Tr("Сглаживание краёв и точность сетки круглых граней. Ниже качество — быстрее показ больших деталей."));

    ui::FieldLabel(ui, Tr("Качество текстур"));
    static constexpr std::array<const char*, 3> textures{N_("Высокое"), N_("Среднее"), N_("Низкое")};
    if (const int n = ui::Segmented(ui, "textures", textures, app.settings.textureQuality, 3, width); n != app.settings.textureQuality)
        app.settings.textureQuality = n;
    caption(Trf("Картинки на гранях хранятся в видеокарте не крупнее {} точек по большей стороне; больше — уменьшаются. "
                "Ниже качество — меньше памяти и быстрее, но картинка грубее. Файл детали не меняется.",
                Settings::TextureLimit(app.settings.textureQuality)));

    ui::FieldLabel(ui, Tr("Анимации камеры"));
    static constexpr std::array<const char*, 2> anim{N_("Плавные повороты"), N_("Без анимации")};
    const int a = app.settings.animations ? 0 : 1;
    if (const int n = ui::Segmented(ui, "anim", anim, a, 2, width); n != a) app.settings.animations = n == 0;
    caption(Tr("Переходы к видам, «Показать всё», вход в эскиз. Без анимации камера переходит сразу."));
}

/// The "Модули" tab: install a package; the modules in a list that scrolls,
/// one compact row each — a check box to switch it on and off, its name and
/// version, "Удалить" for the ones the user installed; what it is and how it
/// runs under it.
void DrawModuleSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const auto caption = [&](const std::string& text, const Color& color) {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(color));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    };
    const std::filesystem::path folder =
        app.modules && !app.modules->InstallDir().empty() ? app.modules->InstallDir() : platform::ConfigDir() / "modules";

    if (ui::SecondaryButton(ui, "##module-install", Tr("Установить модуль…"), nullptr, {ui.S(190), ui.S(30)}, app.modules != nullptr))
        app.Later([&app] { InstallModule(app); });
    ui::ItemTooltip(ui, Tr("Файл .bzmod — или просто перетащите его в окно"));
    ImGui::SameLine(0, ui.S(16));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui.S(6));
    if (ui::LinkButton(ui, "##modules-folder", Tr("Папка модулей"))) {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (!platform::OpenFolder(folder)) app.notes.Error(Tr("Не удалось открыть папку"));
    }
    caption(Tr("Модуль — файл .bzmod для вашей системы. Установите его кнопкой или перетащите в окно программы: "
               "он заработает сразу, без перезапуска."),
            c.textMuted);
    ImGui::Dummy({0, ui.S(6)});

    if (!app.modules || app.modules->Modules().empty()) {
        caption(Tr("Модули не установлены."), c.textMuted);
        return;
    }
    // Grows with the list up to a height, then scrolls.
    ImGui::SetNextWindowSizeConstraints({width, 0}, {width, ui.S(330)});
    ImGui::BeginChild("##modules-list", {width, 0}, ImGuiChildFlags_AutoResizeY);
    const float box = ImGui::GetFrameHeight();
    const float indent = box + ImGui::GetStyle().ItemInnerSpacing.x;
    using modules::ModuleState;
    for (const modules::ModuleEntry& m : app.modules->Modules()) {
        const std::string moduleId = m.manifest.id;   // the list may change: act after the frame
        ImGui::PushID(moduleId.c_str());

        // The row: [x] Name 1.0.0                                   Удалить
        const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        bool on = m.state == ModuleState::Loaded;
        ImGui::BeginDisabled(m.state == ModuleState::Unsupported);   // nothing to switch: the computer lacks something
        if (ImGui::Checkbox("##on", &on))
            app.Later([&app, moduleId, on] {
                const bool done = on ? app.modules->TurnOn(moduleId) : app.modules->TurnOff(moduleId);
                if (!done && on) app.notes.Error(Tr("Модуль не запустился — причина в списке модулей"));
            });
        ImGui::EndDisabled();
        if (m.state != ModuleState::Unsupported) ui::ItemTooltip(ui, on ? Tr("Выключить модуль") : Tr("Включить модуль"));
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        {
            ui::FontScope font(ui.fonts.strong);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(m.manifest.name.c_str());
        }
        if (!m.manifest.version.empty()) {
            ImGui::SameLine(0, ui.S(6));
            ui::FontScope font(ui.fonts.caption);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(m.manifest.version.c_str());
            ImGui::PopStyleColor();
        }
        const bool removable = app.modules->Removable(m);
        if (removable && app.shell.moduleToRemove != moduleId) {
            // As LinkButton measures itself: its font, 8 px on each side; centred on the row.
            const float w = ui.fonts.medium->CalcTextSizeA(ui.S(12), FLT_MAX, 0, Tr("Удалить")).x + ui.S(16);
            ImGui::SameLine(rowRight - w);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (box - ui.S(26)) / 2);
            if (ui::LinkButton(ui, "##remove", Tr("Удалить"))) app.shell.moduleToRemove = moduleId;
        }

        // Under the name: what it is, how it runs.
        ImGui::Indent(indent);
        if (!m.manifest.description.empty()) caption(m.manifest.description, c.textMuted);
        const char* state = N_("Найден");
        switch (m.state) {
            case ModuleState::Found: state = N_("Не загружен"); break;
            case ModuleState::Loaded: state = N_("Работает"); break;
            case ModuleState::Off: state = N_("Выключен"); break;
            case ModuleState::Unsupported: state = N_("Не подходит для этого компьютера"); break;
            case ModuleState::Failed: state = N_("Не запустился"); break;
            case ModuleState::Crashed: state = N_("Отключён после сбоя"); break;
        }
        const bool bad = m.state == ModuleState::Unsupported || m.state == ModuleState::Failed || m.state == ModuleState::Crashed;
        caption(m.problem.empty() ? std::string(Tr(state)) : Trf("{}: {}", Tr(state), Tr(m.problem)),
                bad ? c.danger : m.state == ModuleState::Loaded ? c.success : c.textMuted);
        if (removable && app.shell.moduleToRemove == moduleId) {   // asked once more: its files go
            caption(Tr("Удалить модуль вместе с его файлами?"), c.text);
            if (ui::SecondaryButton(ui, "##remove-yes", Tr("Да, удалить"), nullptr, {ui.S(120), ui.S(26)}))
                app.Later([&app, moduleId] {
                    app.shell.moduleToRemove.clear();
                    const Status removed = app.modules->Uninstall(moduleId);
                    if (!removed) app.notes.Error(Trf("Модуль не удалён: {}", Tr(removed.GetError().message)));
                });
            ImGui::SameLine(0, ui.S(12));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui.S(4));
            if (ui::LinkButton(ui, "##remove-no", Tr("Отмена"))) app.shell.moduleToRemove.clear();
        }
        ImGui::Unindent(indent);
        ImGui::Dummy({0, ui.S(8)});
        ImGui::PopID();
    }
    ImGui::EndChild();
}

/// The "Мышь" tab: a ready-made set of view gestures, or each one by hand.
void DrawMouseSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    Settings& s = app.settings;
    ui::FieldLabel(ui, Tr("Управление видом"));
    const MousePreset* base = FindMousePreset(s.mousePreset);
    if (!base) base = &MousePresets().front();
    const std::string shown = s.mouseCustom ? Trf("Своё (на основе: {})", Tr(base->label)) : std::string(Tr(base->label));
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##mouse-preset", shown.c_str())) {
        for (const MousePreset& p : MousePresets())
            if (ImGui::Selectable(std::string(Tr(p.label)).c_str(), !s.mouseCustom && &p == base)) {
                s.mouse = p.navigation;
                s.mousePreset = std::string(p.id);
                s.mouseCustom = false;
            }
        ImGui::EndCombo();
    }
    ImGui::Dummy({0, ui.S(6)});

    const auto gestureRow = [&](const char* label, const char* id, MouseGesture& g, bool optional) {
        ui::FieldLabel(ui, label);
        ImGui::PushID(id);
        const float half = (width - ui.S(8)) / 2;
        static constexpr std::array<const char*, 3> buttons{N_("нет"), N_("ПКМ"), N_("колесо")};
        const int b = static_cast<int>(g.button);
        const int nb = ui::Segmented(ui, "button", buttons, b, 3, half, optional ? 0 : 0b001);
        ImGui::SameLine(0, ui.S(8));
        static constexpr std::array<const char*, 4> mods{"—", "Shift", "Ctrl", "Alt"};
        const int md = static_cast<int>(g.modifier);
        const int nm = ui::Segmented(ui, "mod", mods, md, 4, half, g.button == MouseButtonChoice::None ? 0b1111 : 0);
        ImGui::PopID();
        if (nb != b || nm != md) {
            g.button = static_cast<MouseButtonChoice>(nb);
            g.modifier = static_cast<ModifierChoice>(nm);
            s.mouseCustom = true;
        }
        ImGui::Dummy({0, ui.S(4)});
    };
    gestureRow(Tr("Вращение"), "orbit", s.mouse.orbit, false);
    gestureRow(Tr("Сдвиг"), "pan", s.mouse.pan, false);
    gestureRow(Tr("Масштаб перетаскиванием (колесо прокрутки масштабирует всегда)"), "zoom", s.mouse.zoom, true);

    // Two gestures on the same button and modifiers cannot both work.
    const auto same = [](const MouseGesture& a, const MouseGesture& b) { return a.button != MouseButtonChoice::None && a == b; };
    if (same(s.mouse.orbit, s.mouse.pan) || same(s.mouse.orbit, s.mouse.zoom) || same(s.mouse.pan, s.mouse.zoom)) {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.danger));
        ImGui::TextUnformatted(Tr("Два действия на одном и том же жесте — сработает только первое (вращение, затем сдвиг)."));
        ImGui::PopStyleColor();
    }
    static constexpr std::array<const char*, 2> wheel{N_("Вперёд — приблизить"), N_("Вперёд — отдалить")};
    ui::FieldLabel(ui, Tr("Колесо"));
    const int w = s.mouse.invertWheel ? 1 : 0;
    if (const int nw = ui::Segmented(ui, "wheel", wheel, w, 2, width); nw != w) {
        s.mouse.invertWheel = nw == 1;
        s.mouseCustom = true;
    }
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("Скорость вращения"));
    {
        static constexpr std::array<const char*, 4> speeds{N_("Медленно"), N_("Обычно"), N_("Быстро"), N_("Очень быстро")};
        static constexpr std::array<double, 4> values{0.5, 1.0, 1.5, 2.0};
        int current = 1;
        for (std::size_t i = 0; i < values.size(); ++i)
            if (std::abs(values[i] - s.orbitSpeed) < 1e-6) current = static_cast<int>(i);
        if (const int n = ui::Segmented(ui, "orbit-speed", speeds, current, 4, width); n != current)
            s.orbitSpeed = values[static_cast<std::size_t>(n)];
        static constexpr std::array<const char*, 2> vertical{N_("Как тянешь"), N_("Наоборот")};
        const int inv = s.invertOrbit ? 1 : 0;
        ui::FieldLabel(ui, Tr("Вращение по вертикали"));
        if (const int n = ui::Segmented(ui, "orbit-invert", vertical, inv, 2, width); n != inv) s.invertOrbit = n == 1;
        static constexpr std::array<const char*, 2> sideways{N_("Свободное"), N_("Поворотный стол")};
        const int table = s.turntableOrbit ? 1 : 0;
        ui::FieldLabel(ui, Tr("Вращение вбок"));
        if (const int n = ui::Segmented(ui, "orbit-turntable", sideways, table, 2, width); n != table) s.turntableOrbit = n == 1;
    }
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(Tr("Короткий щелчок ПКМ открывает меню, короткий щелчок колесом подтверждает операцию — при любом наборе."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("Жесты правой кнопкой"));
    static constexpr std::array<const char*, 2> flick{N_("Рывок — команда"), N_("Выключены")};
    const int fl = s.flickGestures ? 0 : 1;
    if (const int n = ui::Segmented(ui, "flick", flick, fl, 2, width); n != fl) s.flickGestures = n == 0;
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(Tr("Нажмите правую кнопку и резко потяните в сторону — сразу выполнится команда радиального меню в "
                                  "этом направлении, как в SolidWorks. Медленное движение по-прежнему вращает вид, удержание на "
                                  "месте открывает меню."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

/// "Настройки": interface scale and theme; keyboard shortcuts.
/// "Общие" → what is not about the controls or the picture.
void DrawGeneralSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    DrawLanguageSettings(app, width);
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("При запуске"));
    static constexpr std::array<const char*, 2> start{N_("Главная страница"), N_("Последний файл")};
    const int st = app.settings.reopenLast ? 1 : 0;
    if (const int n = ui::Segmented(ui, "start", start, st, 2, width); n != st) app.settings.reopenLast = n == 1;
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("Единицы новой детали"));
    static constexpr std::array<const char*, 4> units{N_("мм"), N_("см"), N_("м"), N_("дюйм")};
    if (const int n = ui::Segmented(ui, "new-units", units, app.settings.newPartLength, 4, width); n != app.settings.newPartLength)
        app.settings.newPartLength = n;
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("Шаг сетки эскиза, мм"));
    static constexpr std::array<const char*, 5> grids{N_("Авто"), "0.5", "1", "5", "10"};
    static constexpr std::array<double, 5> steps{0.0, 0.5, 1.0, 5.0, 10.0};
    int g = 0;
    for (std::size_t i = 0; i < steps.size(); ++i)
        if (std::abs(steps[i] - app.settings.gridStep) < 1e-9) g = static_cast<int>(i);
    if (const int n = ui::Segmented(ui, "grid", grids, g, 5, width); n != g) app.settings.gridStep = steps[static_cast<std::size_t>(n)];
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("Автосохранение"));
    static constexpr std::array<const char*, 4> autosave{N_("Выключено"), N_("2 мин"), N_("5 мин"), N_("10 мин")};
    static constexpr std::array<int, 4> minutes{0, 2, 5, 10};
    int a = 2;
    for (std::size_t i = 0; i < minutes.size(); ++i)
        if (minutes[i] == app.settings.autosaveMinutes) a = static_cast<int>(i);
    if (const int n = ui::Segmented(ui, "autosave", autosave, a, 4, width); n != a)
        app.settings.autosaveMinutes = minutes[static_cast<std::size_t>(n)];
}

/// "Управление" → "Клавиатура": the set of shortcuts and each one.
void DrawKeyboardSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    // The set of shortcuts: a ready-made one, or "Свой" once changed by hand.
    ui::FieldLabel(ui, Tr("Набор сочетаний"));
    const ShortcutPreset* base = FindShortcutPreset(app.settings.shortcutPreset);
    if (!base) base = &ShortcutPresets().front();
    const std::string shown = app.settings.shortcutsCustom ? Trf("Свой (на основе: {})", Tr(base->label))
                                                           : std::string(Tr(base->label));
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##preset", shown.c_str())) {
        for (const ShortcutPreset& p : ShortcutPresets())
            if (ImGui::Selectable(std::string(Tr(p.label)).c_str(), !app.settings.shortcutsCustom && &p == base)) {
                app.actions.ResetChords();
                for (const auto& [id, chord] : p.changes) app.actions.SetChord(id, ActionRegistry::ParseChord(chord));
                app.settings.shortcutPreset = std::string(p.id);
                app.settings.shortcutsCustom = false;
                app.shell.shortcutNote.clear();
                StoreShortcuts(app);
            }
        ImGui::EndCombo();
    }
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::TextUnformatted(Tr("Наборы повторяют привычные клавиши других программ; их названия принадлежат правообладателям."));
        ImGui::PopStyleColor();
    }
    ImGui::Dummy({0, ui.S(4)});
    ui::TextField(ui, "##shortcut-filter", app.shell.shortcutFilter, width, Tr("Поиск команды"));
    ImGui::Dummy({0, ui.S(2)});
    // Capturing: the first chord pressed (a lone modifier does not count).
    if (!app.shell.captureShortcut.empty()) {
        for (ImGuiKeyChord chord : app.pressedChords) {
            const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
            if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl || key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt || key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper)
                continue;
            if (key == ImGuiKey_Escape) {
                app.shell.captureShortcut.clear();
                break;
            }
            const ImGuiKeyChord value = key == ImGuiKey_Backspace ? 0 : chord;
            const std::string taken = app.actions.SetChord(app.shell.captureShortcut, value);
            app.shell.shortcutNote.clear();
            if (!taken.empty())
                if (const Action* other = app.actions.Find(taken))
                    app.shell.shortcutNote = Trf("{} было у «{}» — снято оттуда", ActionRegistry::ChordText(value), other->Label());
            app.shell.captureShortcut.clear();
            app.settings.shortcutsCustom = true;
            StoreShortcuts(app);
            break;
        }
    }
    if (!app.shell.shortcutNote.empty()) {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.warning));
        ImGui::TextUnformatted(app.shell.shortcutNote.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::BeginChild("##shortcuts", {width, ui.S(360)}, ImGuiChildFlags_Borders);
    std::string filter = app.shell.shortcutFilter;
    for (const Action& a : app.actions.All()) {
        if (a.label.empty() || !a.run) continue;
        if (!filter.empty() && std::string_view(a.Label()).find(filter) == std::string_view::npos && a.label.find(filter) == std::string::npos)
            continue;
        ImGui::PushID(a.id.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float rowW = ImGui::GetContentRegionAvail().x, h = ui.S(30);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!a.icon.empty()) ui.icons.Draw(dl, a.icon, {p.x + ui.S(6), p.y + ui.S(6)}, ui.S(18), c.textSecondary);
        dl->AddText(ui.fonts.body, ui.S(13), {p.x + ui.S(32), p.y + ui.S(8)}, ui::ToU32(c.text), a.Label());
        const bool capturing = app.shell.captureShortcut == a.id;
        const std::string chord = capturing ? std::string(Tr("Нажмите сочетание…")) : a.chord ? ActionRegistry::ChordText(a.chord) : "—";
        const float bw = ui.S(170);
        ImGui::SetCursorScreenPos({p.x + rowW - bw - ui.S(4), p.y + ui.S(3)});
        if (ui::SecondaryButton(ui, "##chord", chord.c_str(), nullptr, {bw, h - ui.S(6)})) {
            app.shell.captureShortcut = capturing ? std::string() : a.id;
            app.shell.shortcutNote.clear();
        }
        if (a.chord != a.defaultChord) dl->AddCircleFilled({p.x + rowW - bw - ui.S(12), p.y + h / 2}, ui.S(3), ui::ToU32(c.accent));
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy({rowW, h});   // the row's extent, for the scrolling list
        ImGui::PopID();
    }
    ImGui::EndChild();
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::TextUnformatted(Tr("Щёлкните сочетание и нажмите новое. Esc — отмена, Backspace — убрать. Точка — изменено."));
        ImGui::PopStyleColor();
    }
    if (ui::SecondaryButton(ui, "##reset-keys", Tr("Сбросить все сочетания"), nullptr, {ui.S(196), ui.S(32)})) {
        app.actions.ResetChords();
        app.settings.shortcutPreset = "breezecad";
        app.settings.shortcutsCustom = false;
        StoreShortcuts(app);
        app.shell.shortcutNote.clear();
    }
    // A set of shortcuts as a file: to carry to another computer or share.
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##export-keys", Tr("Сохранить в файл…"), nullptr, {ui.S(176), ui.S(32)})) {
        if (const auto file = platform::ShowSaveDialog(Tr("Сочетания клавиш"), "breezecad-keys.json",
                                                       {{Tr("Сочетания клавиш"), "*.json"}}, ".json")) {
            nlohmann::json j = nlohmann::json::object();
            for (const Action& a : app.actions.All())
                if (!a.label.empty() && a.run) j[a.id] = ActionRegistry::ChordText(a.chord);
            std::ofstream(*file, std::ios::binary) << j.dump(2);
            app.notes.Success(Tr("Сочетания сохранены"));
        }
    }
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##import-keys", Tr("Загрузить из файла…"), nullptr, {ui.S(176), ui.S(32)})) {
        if (const auto file = platform::ShowOpenDialog(Tr("Сочетания клавиш"), {{Tr("Сочетания клавиш"), "*.json"}}, {})) {
            std::ifstream in(*file, std::ios::binary);
            const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_discarded() || !j.is_object()) {
                app.notes.Error(Tr("Файл сочетаний повреждён"));
            } else {
                app.actions.ResetChords();
                for (const auto& [id, chord] : j.items())
                    if (chord.is_string()) app.actions.SetChord(id, ActionRegistry::ParseChord(chord.get<std::string>()));
                app.settings.shortcutsCustom = true;
                StoreShortcuts(app);
                app.notes.Success(Tr("Сочетания загружены"));
            }
        }
    }
}

#if defined(__ANDROID__)
/// "Управление" → "Сенсор" (Android): the phone layout and touch (ADR-0004,
/// "Настройки раскладки"), with "Сбросить".
void DrawTouchScreenSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    MobileSettings& m = app.settings.mobile;
    const auto gap = [&] { ImGui::Dummy({0, ui.S(6)}); };
    ui::FieldLabel(ui, Tr("Вид интерфейса"));
    static constexpr std::array<const char*, 3> layouts{N_("По размеру экрана"), N_("Десктопный"), N_("Мобильный")};
    if (const int n = ui::Segmented(ui, "layout", layouts, m.layout, 3, width); n != m.layout) m.layout = n;
    gap();
    ui::FieldLabel(ui, Tr("Рука"));
    static constexpr std::array<const char*, 2> hands{N_("Правая: дерево слева"), N_("Левая: дерево справа")};
    if (const int n = ui::Segmented(ui, "hand", hands, m.leftHanded ? 1 : 0, 2, width); n != (m.leftHanded ? 1 : 0)) m.leftHanded = n == 1;
    gap();
    ui::FieldLabel(ui, Tr("Ширина левого блока"));
    static constexpr std::array<const char*, 4> lefts{"15%", "20%", "25%", "30%"};
    static constexpr std::array<int, 4> leftValues{15, 20, 25, 30};
    int l = 1;
    for (std::size_t i = 0; i < leftValues.size(); ++i)
        if (leftValues[i] == m.leftWidth) l = static_cast<int>(i);
    if (const int n = ui::Segmented(ui, "left-width", lefts, l, 4, width); n != l) m.leftWidth = leftValues[static_cast<std::size_t>(n)];
    gap();
    ui::FieldLabel(ui, Tr("Блок инструментов"));
    static constexpr std::array<const char*, 4> rights{N_("Только значки"), "15%", "20%", "25%"};
    static constexpr std::array<int, 4> rightValues{10, 15, 20, 25};
    int r = 2;
    if (m.iconsOnly) r = 0;
    else
        for (std::size_t i = 1; i < rightValues.size(); ++i)
            if (rightValues[i] == m.rightWidth) r = static_cast<int>(i);
    if (const int n = ui::Segmented(ui, "right-width", rights, r, 4, width); n != r) {
        m.iconsOnly = n == 0;
        m.rightWidth = rightValues[static_cast<std::size_t>(n)];
    }
    gap();
    ui::FieldLabel(ui, Tr("Подписи в рабочей области (курсор)"));
    static constexpr std::array<const char*, 4> labels{"80%", "100%", "125%", "150%"};
    static constexpr std::array<double, 4> labelValues{0.8, 1.0, 1.25, 1.5};
    int s = 1;
    for (std::size_t i = 0; i < labelValues.size(); ++i)
        if (std::abs(labelValues[i] - m.labelScale) < 1e-6) s = static_cast<int>(i);
    if (const int n = ui::Segmented(ui, "label-scale", labels, s, 4, width); n != s) m.labelScale = labelValues[static_cast<std::size_t>(n)];
    gap();
    const auto hintText = [&](const char* hint) {
        ui::FontScope small(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textMuted));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(hint);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    };
    // Sensitivity: sliders, as in games (a value between the marks is fine).
    ImGui::Dummy({0, ui.S(6)});
    {
        ui::FontScope font(ui.fonts.medium);
        ImGui::TextUnformatted(Tr("Чувствительность"));
    }
    gap();
    const auto slider = [&](const char* id, const char* label, double& value, double lo, double hi) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ui::FieldLabel(ui, label);
        const std::string shown = std::format("{:.0f}%", value * 100.0);
        const ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
        ImGui::GetWindowDrawList()->AddText({at.x + width - ts.x, at.y}, ui::ToU32(ui.C().textSecondary), shown.c_str());
        float t = static_cast<float>((value - lo) / (hi - lo));
        if (ui::Slider(ui, id, t, width)) value = std::round((lo + t * (hi - lo)) * 20.0) / 20.0;   // 5% steps
        gap();
    };
    slider("##finger-orbit", Tr("Вращение"), m.fingerOrbit, 0.25, 4.0);
    slider("##finger-pan", Tr("Сдвиг"), m.fingerPan, 0.25, 3.0);
    slider("##finger-zoom", Tr("Масштаб (щипок)"), m.fingerZoom, 0.25, 3.0);
    slider("##scroll-speed", Tr("Прокрутка списков"), m.scrollSpeed, 0.25, 3.0);
    slider("##cursor-speed", Tr("Скорость курсора"), m.cursorSpeed, 0.1, 1.0);
    hintText(Tr("100% — кончик идёт за пальцем. Меньше — точный режим: кончик проходит часть пути пальца, "
                "тап по-прежнему ставит его в точку касания."));
    gap();
    // Responsiveness: how the fingers are read (mobile/Gestures, mobile/TouchInput).
    ImGui::Dummy({0, ui.S(6)});
    {
        ui::FontScope font(ui.fonts.medium);
        ImGui::TextUnformatted(Tr("Отзывчивость"));
    }
    gap();
    const auto choice = [&](const char* id, const char* label, const char* hint, auto& value, const auto& names, const auto& values) {
        ui::FieldLabel(ui, label);
        int at = -1;
        for (std::size_t i = 0; i < values.size(); ++i)
            if (std::abs(static_cast<double>(values[i]) - static_cast<double>(value)) < 1e-6) at = static_cast<int>(i);
        const int shown = at < 0 ? 0 : at;
        if (const int n = ui::Segmented(ui, id, names, shown, static_cast<int>(names.size()), width); n != shown)
            value = values[static_cast<std::size_t>(n)];
        if (hint) hintText(hint);
        gap();
    };
    static constexpr std::array<const char*, 4> slopNames{N_("Чуткий"), N_("Обычный"), N_("Спокойный"), N_("Твёрдый")};
    static constexpr std::array<int, 4> slopValues{6, 10, 16, 24};
    choice("tap-slop", Tr("Порог касания"),
           Tr("Насколько может сдвинуться палец, чтобы касание осталось нажатием. Если нажатия срываются в "
              "прокрутку или вращение — выберите «Спокойный» или «Твёрдый»."),
           m.tapSlop, slopNames, slopValues);
    static constexpr std::array<const char*, 4> pressNames{"0,3 с", "0,5 с", "0,7 с", "1 с"};
    static constexpr std::array<double, 4> pressValues{0.3, 0.5, 0.7, 1.0};
    choice("long-press", Tr("Долгое нажатие (контекстное меню)"), nullptr, m.longPress, pressNames, pressValues);
    static constexpr std::array<const char*, 4> reachNames{"32 dp", "48 dp", "64 dp", "80 dp"};
    static constexpr std::array<int, 4> reachValues{32, 48, 64, 80};
    choice("cursor-reach", Tr("Кончик курсора над пальцем"), nullptr, m.cursorReach, reachNames, reachValues);

    ui::FieldLabel(ui, Tr("Кнопки громкости"));
    static constexpr std::array<const char*, 2> volume{N_("Громкость"), N_("Масштаб вида")};
    if (const int n = ui::Segmented(ui, "volume", volume, m.volumeZoom ? 1 : 0, 2, width); n != (m.volumeZoom ? 1 : 0)) m.volumeZoom = n == 1;
    gap();
    if (ui::SecondaryButton(ui, "##reset-mobile", Tr("Сбросить настройки"), nullptr, {ui.S(180), ui.S(34)})) {
        // Everything on this page; the gestures have their own tab and button.
        MobileSettings fresh;
        fresh.askedNotifications = m.askedNotifications;
        fresh.projectsFolderName = m.projectsFolderName;
        fresh.hotbar = m.hotbar;
        fresh.gesturesOff = m.gesturesOff;
        fresh.gesturesOn = m.gesturesOn;
        m = fresh;
    }
}

/// "Управление" → "Жесты" (Android): what fingers do over the view, one set
/// with the cursor off and one with it on (the author's idea, 17.10.2026).
void DrawGestureSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    MobileSettings& m = app.settings.mobile;
    const auto gap = [&] { ImGui::Dummy({0, ui.S(6)}); };
    static int cursorTab = 0;
    static constexpr std::array<const char*, 2> sets{N_("Курсор выкл"), N_("Курсор вкл")};
    cursorTab = ui::Segmented(ui, "gesture-set", sets, cursorTab, 2, width);
    gap();
    const bool on = cursorTab == 1;
    FingerGestures& g = on ? m.gesturesOn : m.gesturesOff;

    static constexpr std::array<const char*, 4> drags{N_("Вращение"), N_("Сдвиг"), N_("Масштаб"), N_("Нет")};
    // FingerDrag: None 0, Orbit 1, Pan 2, Zoom 3 — shown Orbit, Pan, Zoom, None.
    const auto dragRow = [&](const char* id, const char* label, mobile::FingerDrag& d) {
        ui::FieldLabel(ui, label);
        const int shown = d == mobile::FingerDrag::None ? 3 : static_cast<int>(d) - 1;
        if (const int n = ui::Segmented(ui, id, drags, shown, 4, width); n != shown)
            d = n == 3 ? mobile::FingerDrag::None : static_cast<mobile::FingerDrag>(n + 1);
        gap();
    };
    if (on) {
        ui::FieldLabel(ui, Tr("Один палец"));
        ui::FontScope small(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textMuted));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(Tr("Ведёт курсор — это право первого пальца, пока курсор включён. Тап ставит кончик в точку."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        gap();
    } else {
        dragRow("drag-1", Tr("Один палец"), g.drags.one);
    }
    dragRow("drag-2", Tr("Два пальца"), g.drags.two);
    dragRow("drag-3", Tr("Три пальца"), g.drags.three);
    ui::FieldLabel(ui, Tr("Щипок"));
    static constexpr std::array<const char*, 2> pinch{N_("Масштаб"), N_("Нет")};
    if (const int n = ui::Segmented(ui, "pinch", pinch, g.drags.pinch ? 0 : 1, 2, width); n != (g.drags.pinch ? 0 : 1)) g.drags.pinch = n == 0;
    gap();
    static constexpr std::array<const char*, 5> taps{N_("Отменить"), N_("Повторить"), N_("Показать всё"), N_("Домой"), N_("Нет")};
    // FingerTap: None 0, Undo 1, Redo 2, Fit 3, Home 4 — shown Undo, Redo, Fit, Home, None.
    const auto tapRow = [&](const char* id, const char* label, FingerTap& t) {
        ui::FieldLabel(ui, label);
        const int shown = t == FingerTap::None ? 4 : static_cast<int>(t) - 1;
        if (const int n = ui::Segmented(ui, id, taps, shown, 3, width); n != shown)
            t = n == 4 ? FingerTap::None : static_cast<FingerTap>(n + 1);
        gap();
    };
    tapRow("tap-2", Tr("Тап двумя пальцами"), g.tap2);
    tapRow("tap-3", Tr("Тап тремя пальцами"), g.tap3);
    {
        ui::FontScope small(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textMuted));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(Tr("Тап одним пальцем — выбор (без курсора), долгое нажатие — контекстное меню."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    gap();
    if (ui::SecondaryButton(ui, "##reset-gestures", Tr("Сбросить жесты"), nullptr, {ui.S(180), ui.S(34)})) {
        m.gesturesOff = FingerGestures{};
        m.gesturesOn = FingerGestures{mobile::GestureRecognizer::DefaultDrags(true)};
    }
}
#endif

/// "Графика" → "Внешний вид": scale, theme, the axes in the corner, background.
void DrawAppearanceSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ui::FieldLabel(ui, Tr("Масштаб интерфейса"));
    static constexpr std::array<const char*, 10> scales{N_("Авто"), "75%", "85%", "90%", "100%", "110%", "125%", "150%", "175%", "200%"};
    static constexpr std::array<double, 10> values{0, 0.75, 0.85, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0};
    int current = 0;
    for (std::size_t i = 0; i < values.size(); ++i)
        if (std::abs(values[i] - app.settings.uiScale) < 1e-6) current = static_cast<int>(i);
    const int chosen = ui::Segmented(ui, "scale", scales, current, 5, width);
    if (chosen != current) app.settings.uiScale = values[static_cast<std::size_t>(chosen)];
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(Trf("Сейчас {:.0f}%. «Авто» берёт масштаб экрана из системы и уменьшает интерфейс на небольших "
                                   "экранах (1366×768), чтобы всё помещалось.", ui.scale * 100.0).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("Оформление"));
    static constexpr std::array<const char*, 3> themes{N_("Как в системе"), N_("Светлая"), N_("Тёмная")};
    const int theme = static_cast<int>(app.settings.theme);
    if (const int t = ui::Segmented(ui, "theme", themes, theme, 3, width); t != theme) SetTheme(app, static_cast<ThemeChoice>(t));

    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("Оси в углу вида"));
    static constexpr std::array<const char*, 2> triad{N_("Показывать"), N_("Скрыть")};
    const int t = app.settings.showTriad ? 0 : 1;
    if (const int n = ui::Segmented(ui, "triad", triad, t, 2, width); n != t) app.settings.showTriad = n == 0;
    ImGui::Dummy({0, ui.S(6)});
    ui::FieldLabel(ui, Tr("Фон вида"));
    if (ui::LinkButton(ui, "##open-backgrounds", Tr("Выбрать на вкладке «Вид» → «Фон»…"))) app.shell.backgroundMenu = true;
}

#if defined(__ANDROID__)
// --- Settings on the phone, as in Minecraft (docs/dev/mockups/4, the author 17.10.2026) ---
// Full screen next to the hotbar: big tabs on the left, the groups as pills
// on top, the options as a grid of two columns of wide cells. A slider is the
// cell itself (drag across it, the value in its caption); a choice is a cell
// that steps to the next value with a tap.
namespace phone {

/// A cell `w` × `h` at the cursor; the grid moves on by itself.
struct Grid {
    ui::Ui& ui;
    float   x, width, gap;
    float   y;
    int     column = 0;
    float   CellW() const { return (width - gap) / 2; }
    ImVec2  Next(bool wide) {
        if (wide && column == 1) {
            column = 0;
            y += ui.S(44) + gap;
        }
        const ImVec2 at{x + (column == 1 ? CellW() + gap : 0.0f), y};
        if (wide || column == 1) {
            column = 0;
            y += ui.S(44) + gap;
        } else {
            column = 1;
        }
        return at;
    }
    void Finish() {
        if (column == 1) {
            column = 0;
            y += ui.S(44) + gap;
        }
        ImGui::SetCursorScreenPos({x, y});
        ImGui::Dummy({width, ui.S(8)});
    }
};

void CellFrame(ui::Ui& ui, ImVec2 a, ImVec2 b, bool held, bool button, Color border) {
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, ui::ToU32(held ? c.pressed : button ? c.field : c.panel), ui.S(12));
    dl->AddRect(a, b, ui::ToU32(border), ui.S(12), 0, ui.S(1));
}

void CellText(ui::Ui& ui, ImVec2 a, ImVec2 b, const std::string& text, Color ink) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, text.c_str());
    const float x = std::max(a.x + ui.S(10), (a.x + b.x - ts.x) / 2);
    ImGui::PushClipRect(a, b, true);
    dl->AddText(ui.fonts.medium, ui.S(13), {x, (a.y + b.y - ts.y) / 2}, ui::ToU32(ink), text.c_str());
    ImGui::PopClipRect();
}

/// A slider cell: "label: value" over a fill to the value.
void Slider(Grid& g, const char* id, const char* label, double& value, double lo, double hi, double step, const std::string& shown) {
    ui::Ui& ui = g.ui;
    const ui::ThemeColors& c = ui.C();
    const ImVec2 a = g.Next(false), b{a.x + g.CellW(), a.y + ui.S(44)};
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton(id, {b.x - a.x, b.y - a.y});
    if (ImGui::IsItemActive()) {
        const double t = std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x - a.x) / (b.x - a.x)), 0.0, 1.0);
        value = std::clamp(lo + std::round(t * (hi - lo) / step) * step, lo, hi);
    }
    CellFrame(ui, a, b, false, false, ImGui::IsItemActive() ? c.selection : c.fieldBorder);
    const float t = static_cast<float>((value - lo) / (hi - lo));
    const float x = a.x + (b.x - a.x) * std::clamp(t, 0.0f, 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({a.x + 1, a.y + 1}, {std::max(a.x + 1, x), b.y - 1}, ui::ToU32(c.accent.WithAlpha(0.20f)), ui.S(12),
                      ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled({x - ui.S(2), a.y + 1}, {x + ui.S(2), b.y - 1}, ui::ToU32(c.selection));
    CellText(ui, a, b, std::string(label) + ": " + shown, c.text);
}

/// A choice cell: a tap steps to the next value.
template <std::size_t N>
bool Choice(Grid& g, const char* id, const char* label, const std::array<const char*, N>& names, int& index, bool wide = false) {
    ui::Ui& ui = g.ui;
    const ui::ThemeColors& c = ui.C();
    const ImVec2 a = g.Next(wide), b{a.x + (wide ? g.width : g.CellW()), a.y + ui.S(44)};
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton(id, {b.x - a.x, b.y - a.y});
    if (clicked) index = (index + 1) % static_cast<int>(N);
    CellFrame(ui, a, b, ImGui::IsItemActive(), true, c.fieldBorder);
    CellText(ui, a, b, std::string(label) + ": " + Tr(names[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(N) - 1))]), c.text);
    return clicked;
}

/// An action cell; `left` puts the icon and text at the start (else at the end).
bool Action(Grid& g, const char* id, const char* icon, const char* label, Color ink, Color border, bool left) {
    ui::Ui& ui = g.ui;
    const ImVec2 a = g.Next(false), b{a.x + g.CellW(), a.y + ui.S(44)};
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton(id, {b.x - a.x, b.y - a.y});
    CellFrame(ui, a, b, ImGui::IsItemActive(), true, border);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, label);
    const float is = ui.S(17);
    const float x = left ? a.x + ui.S(16) : b.x - ui.S(16) - ts.x - is - ui.S(10);
    ui.icons.Draw(dl, icon, {x, (a.y + b.y - is) / 2}, is, ink);
    dl->AddText(ui.fonts.medium, ui.S(13), {x + is + ui.S(10), (a.y + b.y - ts.y) / 2}, ui::ToU32(ink), label);
    return clicked;
}

void Heading(Grid& g, const char* text) {
    g.Finish();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(g.ui.fonts.strong, g.ui.S(12), {g.x + g.ui.S(4), g.y + g.ui.S(4)}, ui::ToU32(g.ui.C().textMuted), text);
    g.y += g.ui.S(24);
}

template <typename T, std::size_t N>
int IndexOf(const std::array<T, N>& values, T value) {
    for (std::size_t i = 0; i < N; ++i)
        if (std::abs(static_cast<double>(values[i]) - static_cast<double>(value)) < 1e-6) return static_cast<int>(i);
    return 0;
}

void Sensor(AppContext& app, Grid& g) {
    MobileSettings& m = app.settings.mobile;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const auto pct = [](double v) { return std::format("{:.0f} %", v * 100.0); };
    Slider(g, "##p-orbit", Tr("Вращение"), m.fingerOrbit, 0.25, 4.0, 0.05, pct(m.fingerOrbit));
    Slider(g, "##p-pan", Tr("Сдвиг"), m.fingerPan, 0.25, 3.0, 0.05, pct(m.fingerPan));
    Slider(g, "##p-zoom", Tr("Масштаб (щипок)"), m.fingerZoom, 0.25, 3.0, 0.05, pct(m.fingerZoom));
    Slider(g, "##p-scroll", Tr("Прокрутка списков"), m.scrollSpeed, 0.25, 3.0, 0.05, pct(m.scrollSpeed));
    Slider(g, "##p-cursor", Tr("Скорость курсора"), m.cursorSpeed, 0.1, 1.0, 0.05, pct(m.cursorSpeed));
    {
        static constexpr std::array<const char*, 4> names{N_("Чуткий"), N_("Обычный"), N_("Спокойный"), N_("Твёрдый")};
        static constexpr std::array<int, 4> values{6, 10, 16, 24};
        int i = IndexOf(values, m.tapSlop);
        if (Choice(g, "##p-slop", Tr("Порог касания"), names, i)) m.tapSlop = values[static_cast<std::size_t>(i)];
    }
    {
        static constexpr std::array<const char*, 4> names{"0,3 с", "0,5 с", "0,7 с", "1 с"};
        static constexpr std::array<double, 4> values{0.3, 0.5, 0.7, 1.0};
        int i = IndexOf(values, m.longPress);
        if (Choice(g, "##p-press", Tr("Долгое нажатие"), names, i)) m.longPress = values[static_cast<std::size_t>(i)];
    }
    {
        static constexpr std::array<const char*, 4> names{"32 dp", "48 dp", "64 dp", "80 dp"};
        static constexpr std::array<int, 4> values{32, 48, 64, 80};
        int i = IndexOf(values, m.cursorReach);
        if (Choice(g, "##p-reach", Tr("Кончик курсора"), names, i)) m.cursorReach = values[static_cast<std::size_t>(i)];
    }
    Heading(g, Tr("Раскладка"));
    {
        static constexpr std::array<const char*, 2> names{N_("правая"), N_("левая")};
        int i = m.leftHanded ? 1 : 0;
        if (Choice(g, "##p-hand", Tr("Рука"), names, i)) m.leftHanded = i == 1;
    }
    {
        static constexpr std::array<const char*, 3> names{N_("компактный"), N_("список"), N_("только значки")};
        int i = m.iconsOnly ? 2 : m.toolsList ? 1 : 0;
        if (Choice(g, "##p-tools", Tr("Блок инструментов"), names, i)) {
            m.toolsList = i == 1;
            m.iconsOnly = i == 2;
            if (!m.iconsOnly && m.rightWidth < 12) m.rightWidth = 20;
        }
    }
    {
        static constexpr std::array<const char*, 3> names{N_("по размеру экрана"), N_("десктопный"), N_("мобильный")};
        Choice(g, "##p-layout", Tr("Вид"), names, m.layout);
    }
    {
        static constexpr std::array<const char*, 4> names{"80%", "100%", "125%", "150%"};
        static constexpr std::array<double, 4> values{0.8, 1.0, 1.25, 1.5};
        int i = IndexOf(values, m.labelScale);
        if (Choice(g, "##p-labels", Tr("Подписи в виде"), names, i)) m.labelScale = values[static_cast<std::size_t>(i)];
    }
    {
        static constexpr std::array<const char*, 2> names{N_("громкость"), N_("масштаб вида")};
        int i = m.volumeZoom ? 1 : 0;
        if (Choice(g, "##p-volume", Tr("Кнопки громкости"), names, i)) m.volumeZoom = i == 1;
    }
    if (Action(g, "##p-setup", "ui-sliders", Tr("Пройти первую настройку"), c.text, c.fieldBorder, true))
        app.Later([&app] {
            app.shell.showSettings = false;
            mobile::StartSetup(app);
        });
    g.Finish();
    // The last row: the layout editor at the start, the reset at the end (the author's wish).
    if (Action(g, "##p-edit-layout", "ui-edit", Tr("Настроить раскладку…"), c.selection, c.accentSoftBorder, true))
        app.Later([&app] { mobile::OpenLayoutEditor(app); });
    if (Action(g, "##p-reset", "ui-trash", Tr("Сбросить настройки"), c.danger, c.fieldBorder, false)) {
        MobileSettings fresh;
        fresh.askedNotifications = m.askedNotifications;
        fresh.projectsFolderName = m.projectsFolderName;
        fresh.hotbar = m.hotbar;
        fresh.gesturesOff = m.gesturesOff;
        fresh.gesturesOn = m.gesturesOn;
        fresh.firstStartDone = m.firstStartDone;
        m = fresh;
    }
    g.Finish();
}

void Gestures(AppContext& app, Grid& g) {
    MobileSettings& m = app.settings.mobile;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    static int set = 0;
    {
        static constexpr std::array<const char*, 2> names{N_("курсор выкл"), N_("курсор вкл")};
        Choice(g, "##g-set", Tr("Набор"), names, set, true);
    }
    FingerGestures& gs = set == 1 ? m.gesturesOn : m.gesturesOff;
    static constexpr std::array<const char*, 4> drags{N_("вращение"), N_("сдвиг"), N_("масштаб"), N_("нет")};
    const auto drag = [&](const char* id, const char* label, mobile::FingerDrag& d) {
        int i = d == mobile::FingerDrag::None ? 3 : static_cast<int>(d) - 1;
        if (Choice(g, id, label, drags, i)) d = i == 3 ? mobile::FingerDrag::None : static_cast<mobile::FingerDrag>(i + 1);
    };
    if (set == 1) {
        static constexpr std::array<const char*, 1> cursor{N_("ведёт курсор")};
        int i = 0;
        Choice(g, "##g-1", Tr("1 палец"), cursor, i);
    } else {
        drag("##g-1", Tr("1 палец"), gs.drags.one);
    }
    drag("##g-2", Tr("2 пальца"), gs.drags.two);
    drag("##g-3", Tr("3 пальца"), gs.drags.three);
    {
        static constexpr std::array<const char*, 2> names{N_("масштаб"), N_("нет")};
        int i = gs.drags.pinch ? 0 : 1;
        if (Choice(g, "##g-pinch", Tr("Щипок"), names, i)) gs.drags.pinch = i == 0;
    }
    static constexpr std::array<const char*, 5> taps{N_("отменить"), N_("повторить"), N_("показать всё"), N_("домой"), N_("нет")};
    const auto tap = [&](const char* id, const char* label, FingerTap& t) {
        int i = t == FingerTap::None ? 4 : static_cast<int>(t) - 1;
        if (Choice(g, id, label, taps, i)) t = i == 4 ? FingerTap::None : static_cast<FingerTap>(i + 1);
    };
    tap("##g-tap2", Tr("Тап 2 пальцами"), gs.tap2);
    tap("##g-tap3", Tr("Тап 3 пальцами"), gs.tap3);
    g.Finish();
    if (Action(g, "##g-reset", "ui-trash", Tr("Сбросить жесты"), c.danger, c.fieldBorder, true)) {
        m.gesturesOff = FingerGestures{};
        m.gesturesOn = FingerGestures{mobile::GestureRecognizer::DefaultDrags(true)};
    }
    g.Finish();
}

/// A big tab on the left.
bool Tab(ui::Ui& ui, const char* id, const char* icon, const char* label, bool on, ImVec2 at, float width) {
    const ui::ThemeColors& c = ui.C();
    const ImVec2 b{at.x + width, at.y + ui.S(46)};
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton(id, {width, b.y - at.y});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, b, ui::ToU32(on ? c.accentSoft : ImGui::IsItemActive() ? c.pressed : c.panel), ui.S(12));
    dl->AddRect(at, b, ui::ToU32(on ? c.accentSoftBorder : c.divider), ui.S(12));
    ui.icons.Draw(dl, icon, {at.x + ui.S(14), (at.y + b.y - ui.S(18)) / 2}, ui.S(18), on ? c.selection : c.textSecondary);
    dl->AddText(ui.fonts.medium, ui.S(13.5f), {at.x + ui.S(44), (at.y + b.y - ui.S(14)) / 2}, ui::ToU32(on ? c.text : c.textSecondary), label);
    return clicked;
}

void Draw(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const mobile::MobileState& ms = app.shell.mobile;
    const ImGuiIO& io = ImGui::GetIO();
    // Next to the hotbar (it stays), over everything else.
    const float left = ms.rHotbar.max.x > 0 && ms.rHotbar.min.x < 1 ? ms.rHotbar.max.x : ms.insets.left;
    const float right = io.DisplaySize.x - (ms.rHotbar.min.x > 1 ? io.DisplaySize.x - ms.rHotbar.min.x : ms.insets.right);
    const ImVec2 pos{left, 0}, size{right - left, io.DisplaySize.y};
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(c.appBackground));
    const bool open = ImGui::Begin("##phone-settings", nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    if (open) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float tabsW = std::clamp(size.x * 0.22f, ui.S(150), ui.S(200));
        dl->AddRectFilled(pos, {pos.x + tabsW, pos.y + size.y}, ui::ToU32(c.ribbon));
        dl->AddLine({pos.x + tabsW, pos.y}, {pos.x + tabsW, pos.y + size.y}, ui::ToU32(c.divider));
        const float tx = pos.x + ui.S(12), tw = tabsW - ui.S(24);
        float ty = pos.y + std::max(ui.S(12), ms.insets.top);
        dl->AddText(ui.fonts.title, ui.S(17), {tx + ui.S(4), ty}, ui::ToU32(c.text), Tr("Настройки"));
        ty += ui.S(32);
        struct T {
            const char* icon;
            const char* label;
        };
        const T tabs[] = {{"ui-settings", Tr("Общие")}, {"ui-sliders", Tr("Управление")}, {"ui-display-style", Tr("Графика")},
                          {"library", Tr("Модули")}};
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            if (Tab(ui, "##tab", tabs[i].icon, tabs[i].label, app.shell.settingsTab == i, {tx, ty}, tw)) {
                app.shell.settingsTab = i;
                app.shell.settingsSub = i == 1 ? 2 : 0;   // controls: the touch screen first
            }
            ImGui::PopID();
            ty += ui.S(54);
        }
        bool done = Tab(ui, "##done", "ui-check", Tr("Готово"), false, {tx, pos.y + size.y - ui.S(58) - ms.insets.bottom}, tw);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && app.shell.captureShortcut.empty()) done = true;

        // The groups as pills on top.
        const float cx = pos.x + tabsW + ui.S(16), cw = size.x - tabsW - ui.S(32);
        float cy = pos.y + std::max(ui.S(12), ms.insets.top);
        std::vector<const char*> subs;
        if (app.shell.settingsTab == 1) subs = {Tr("Клавиатура"), Tr("Мышь"), Tr("Сенсор"), Tr("Жесты")};
        if (app.shell.settingsTab == 2) subs = {Tr("Внешний вид"), Tr("Качество"), Tr("Производительность")};
        if (!subs.empty()) {
            float px = cx;
            for (int i = 0; i < static_cast<int>(subs.size()); ++i) {
                const float w = ui.fonts.body->CalcTextSizeA(ui.S(12.5f), FLT_MAX, 0, subs[static_cast<std::size_t>(i)]).x + ui.S(32);
                const bool on = app.shell.settingsSub == i;
                ImGui::SetCursorScreenPos({px, cy});
                ImGui::PushID(100 + i);
                if (ImGui::InvisibleButton("##sub", {w, ui.S(34)})) app.shell.settingsSub = i;
                ImGui::PopID();
                dl->AddRectFilled({px, cy}, {px + w, cy + ui.S(34)}, ui::ToU32(on ? c.field : c.panel), ui.S(17));
                dl->AddRect({px, cy}, {px + w, cy + ui.S(34)}, ui::ToU32(on ? c.fieldBorder : c.divider), ui.S(17));
                dl->AddText(ui.fonts.body, ui.S(12.5f), {px + ui.S(16), cy + (ui.S(34) - ui.S(13)) / 2}, ui::ToU32(on ? c.text : c.textSecondary),
                            subs[static_cast<std::size_t>(i)]);
                px += w + ui.S(6);
            }
            cy += ui.S(46);
        }
        // The content scrolls (a finger up or down), the grid inside.
        ImGui::SetCursorScreenPos({cx, cy});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::BeginChild("##phone-settings-content", {cw, pos.y + size.y - cy - ms.insets.bottom}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool grid = app.shell.settingsTab == 1 && (app.shell.settingsSub == 2 || app.shell.settingsSub == 3);
        if (grid) {
            Grid g{ui, origin.x, cw, ui.S(8), origin.y};
            if (app.shell.settingsSub == 2) Sensor(app, g);
            else Gestures(app, g);
        } else {
            // The other pages as they are (lists and fields), in the content's width.
            ImGui::PushTextWrapPos(cw);
            switch (app.shell.settingsTab) {
                case 1:
                    if (app.shell.settingsSub == 1) DrawMouseSettings(app, cw);
                    else DrawKeyboardSettings(app, cw);
                    break;
                case 2:
                    if (app.shell.settingsSub == 1) DrawOptimizationSettings(app, cw, false);
                    else if (app.shell.settingsSub == 2) DrawOptimizationSettings(app, cw, true);
                    else DrawAppearanceSettings(app, cw);
                    break;
                case 3: DrawModuleSettings(app, cw); break;
                default: DrawGeneralSettings(app, cw); break;
            }
            ImGui::PopTextWrapPos();
            ImGui::Dummy({cw, ui.S(16)});
        }
        ImGui::EndChild();
        if (done) {
            app.shell.showSettings = false;
            app.shell.captureShortcut.clear();
        }
    }
    ImGui::End();
}

} // namespace phone
#endif

void DrawSettings(AppContext& app) {
    ui::Ui& ui = app.ui;
#if defined(__ANDROID__)
    if (app.shell.mobile.phone) {   // full screen, as in Minecraft
        if (app.shell.showSettings) phone::Draw(app);
        return;
    }
#endif
    if (!BeginDialog(ui, "##settings", app.shell.showSettings, ui.S(700))) return;
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(Tr("Настройки"));
    }
    const float width = ImGui::GetContentRegionAvail().x;
    // Four tabs (the author's plan); "Управление" and "Графика" have groups inside.
    static constexpr std::array<const char*, 4> tabs{N_("Общие"), N_("Управление"), N_("Графика"), N_("Модули")};
    if (const int t = ui::Segmented(ui, "settings-tab", tabs, app.shell.settingsTab, 4, width); t != app.shell.settingsTab) {
        app.shell.settingsTab = t;
        app.shell.settingsSub = 0;
    }
    ImGui::Dummy({0, ui.S(4)});
    if (app.shell.settingsTab == 1) {
#if defined(__ANDROID__)
        // A touch screen instead of a touchpad (ADR-0004): layout, hand, sizes, cursor.
        static constexpr std::array<const char*, 4> subs{N_("Клавиатура"), N_("Мышь"), N_("Сенсор"), N_("Жесты")};
#else
        static constexpr std::array<const char*, 3> subs{N_("Клавиатура"), N_("Мышь"), N_("Тачпад")};
#endif
        app.shell.settingsSub = ui::Segmented(ui, "settings-sub", subs, app.shell.settingsSub, static_cast<int>(subs.size()), width);
        ImGui::Dummy({0, ui.S(4)});
    } else if (app.shell.settingsTab == 2) {
        static constexpr std::array<const char*, 3> subs{N_("Внешний вид"), N_("Качество"), N_("Производительность")};
        app.shell.settingsSub = ui::Segmented(ui, "settings-sub", subs, app.shell.settingsSub, 3, width);
        ImGui::Dummy({0, ui.S(4)});
    }
    ImGui::Dummy({0, ui.S(2)});

    switch (app.shell.settingsTab) {
        case 1:
            if (app.shell.settingsSub == 1) DrawMouseSettings(app, width);
#if defined(__ANDROID__)
            else if (app.shell.settingsSub == 2) DrawTouchScreenSettings(app, width);
            else if (app.shell.settingsSub == 3) DrawGestureSettings(app, width);
#else
            else if (app.shell.settingsSub == 2) DrawTouchpadSettings(app, width);
#endif
            else DrawKeyboardSettings(app, width);
            break;
        case 2:
            if (app.shell.settingsSub == 1) DrawOptimizationSettings(app, width, false);
            else if (app.shell.settingsSub == 2) DrawOptimizationSettings(app, width, true);
            else DrawAppearanceSettings(app, width);
            break;
        case 3: DrawModuleSettings(app, width); break;
        default: DrawGeneralSettings(app, width); break;
    }
    ImGui::Dummy({0, ui.S(8)});
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape) && app.shell.captureShortcut.empty();
    if (ui::PrimaryButton(ui, "##settings-close", Tr("Готово"), nullptr, {ui.S(140), ui.S(34)}) || escape) {
        app.shell.showSettings = false;
        app.shell.captureShortcut.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/// The testers' build: what happened, packed into one zip to send.
void DrawBetaReport(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ShellState::BetaReport& r = app.shell.betaReport;
    if (!BeginDialog(ui, "##beta-report", r.open, ui.S(560))) return;
    const auto paragraph = [&](const std::string& text, Color colour) {
        ui::FontScope font(ui.fonts.body);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(colour));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    };
    const auto close = [&] {
        r = {};
        ImGui::CloseCurrentPopup();
    };
    const float width = ImGui::GetContentRegionAvail().x;
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(r.crash ? Tr("BreezeCAD закрылся с ошибкой") : Tr("Сообщить о проблеме"));
    }
    ImGui::Dummy({0, ui.S(2)});
    if (!r.packed.empty()) {
        paragraph(Tr("Спасибо! Отчёт сохранён в файл:"), c.textSecondary);
        paragraph(ToUtf8(r.packed.filename()), c.text);
        ImGui::Dummy({0, ui.S(2)});
        paragraph(Tr("Отправьте этот файл в чат тестеров. Кнопка ниже откроет папку, где он лежит."), c.textSecondary);
        ImGui::Dummy({0, ui.S(8)});
        const float bw = (width - ui.S(8)) / 2;
#if defined(__ANDROID__)
        // No file manager for the app's own folder: the system's "share" sends the zip (ADR-0004).
        if (ui::PrimaryButton(ui, "##beta-show", Tr("Поделиться отчётом"), nullptr, {bw, ui.S(36)}))
            platform::android::Share(r.packed, "application/zip", "BreezeCAD");
#else
        if (ui::PrimaryButton(ui, "##beta-show", Tr("Открыть папку с отчётом"), nullptr, {bw, ui.S(36)}))
            if (!platform::OpenFolder(r.packed.parent_path())) app.notes.Error(Tr("Не удалось открыть папку"));
#endif
        ImGui::SameLine(0, ui.S(8));
        if (ui::SecondaryButton(ui, "##beta-done", Tr("Закрыть"), "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
        ImGui::EndPopup();
        return;
    }
    if (r.crash)
        paragraph(Trf("Это случилось в прошлый раз ({}). Программа записала, что происходило перед этим. Напишите в двух-трёх "
                      "предложениях, что вы делали — например: «сделал вращение эскиза, потом зеркало, и программа закрылась».",
                      r.started),
                  c.textSecondary);
    else
        paragraph(Tr("Опишите, что не так: что вы делали, что ожидали и что получилось. В отчёт попадёт всё, что программа "
                     "записала за этот запуск (действия, ошибки, скорость работы) и описание компьютера."),
                  c.textSecondary);
    ImGui::Dummy({0, ui.S(4)});
    ImGui::SetNextItemWidth(width);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextMultiline("##beta-text", &r.text, {width, ui.S(120)});
    ImGui::Dummy({0, ui.S(8)});
    const float bw = (width - ui.S(8)) / 2;
    if (ui::PrimaryButton(ui, "##beta-pack", Tr("Сохранить отчёт"), nullptr, {bw, ui.S(36)})) {
        if (const auto zip = beta::Pack(r.session, r.text, r.crash)) r.packed = *zip;
        else app.notes.Error(Tr(zip.GetError().message));
    }
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##beta-skip", r.crash ? Tr("Не сейчас") : Tr("Отмена"), "Esc", {bw, ui.S(36)}) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (r.crash) beta::Dismiss(r.session);
        close();
    }
    ImGui::EndPopup();
}

void DrawDialogs(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    DrawNewDocumentDialog(app);

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
                ImGui::TextUnformatted(Tr("Сохранить изменения?"));
            }
            {
                ui::FontScope font(ui.fonts.body);
                ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textSecondary));
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(Trf("В документе «{}» есть несохранённые изменения. Если не сохранить, они пропадут.", s->Title()).c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0, ui.S(8)});
            const float bw = (ImGui::GetContentRegionAvail().x - ui.S(16)) / 3;
            if (ui::PrimaryButton(ui, "##save", Tr("Сохранить"), nullptr, {bw, ui.S(36)})) {
                if (app.workspace.Save(*s, false, app.notes, app.settings)) {
                    app.autosave.Discard(*s);
                    app.workspace.Close(index);
                    finish(true);
                } else {
                    finish(false);
                }
            }
            ImGui::SameLine(0, ui.S(8));
            if (ui::SecondaryButton(ui, "##discard", Tr("Не сохранять"), nullptr, {bw, ui.S(36)})) {
                app.autosave.Discard(*s);
                app.workspace.Close(index);
                finish(true);
            }
            ImGui::SameLine(0, ui.S(8));
            if (ui::SecondaryButton(ui, "##cancel", Tr("Отмена"), "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                finish(false);
        }
        ImGui::EndPopup();
    }

    DrawMassProperties(app);
    DrawLibrary(app);
    DrawSaveToLibrary(app);
    DrawBackgroundMenu(app);

    DrawSettings(app);
    DrawBetaReport(app);

    // --- About ---
    if (BeginDialog(ui, "##about", app.shell.showAbout, ui.S(480))) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ui.icons.Draw(dl, "brand-mark", p, ui.S(48), {1, 1, 1, 1});
        const std::string title = std::string("BreezeCAD ") + Version();
        dl->AddText(ui.fonts.display, ui.S(24), {p.x + ui.S(62), p.y + ui.S(2)}, ui::ToU32(c.text), title.c_str());
        dl->AddText(ui.fonts.body, ui.S(13), {p.x + ui.S(63), p.y + ui.S(32)}, ui::ToU32(c.textMuted),
                    Tr("Свободный параметрический САПР"));
        // Exactly which build: the commit names it on GitHub.
        std::string build = *Commit() ? Trf("Сборка {} {} от {}", Branch(), Commit(), BuildDate())
                                      : Trf("Сборка от {}", BuildDate());
#if defined(__ANDROID__)
        // The APK's own version and number (the number grows with every build;
        // what a store and "install over" compare).
        if (const std::string package = platform::android::PackageVersion(); !package.empty()) {
            const auto tab = package.find('\t');
            build = Trf("Android {}, версия {}", package.substr(0, tab), tab == std::string::npos ? "" : package.substr(tab + 1)) +
                    "\n" + build;
        }
#endif
        dl->AddText(ui.fonts.mono, ui.S(11), {p.x + ui.S(63), p.y + ui.S(52)}, ui::ToU32(c.textMuted), build.c_str());
#if defined(__ANDROID__)
        ImGui::Dummy({0, ui.S(90)});
#else
        ImGui::Dummy({0, ui.S(74)});
#endif
        {
            ui::FontScope font(ui.fonts.bodySmall);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textSecondary));
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(Tr("Работает полностью офлайн. Никакие данные никуда не отправляются."));
            ImGui::TextUnformatted(Tr("Лицензия MPL-2.0. Файлы .bzc — открытый формат (ZIP + JSON)."));
            ImGui::Dummy({0, ui.S(4)});
            ImGui::TextUnformatted(Tr("Использует: Open CASCADE Technology (LGPL 2.1), Dear ImGui, GLFW, lunasvg, "
                                   "nlohmann/json, miniz, portable-file-dialogs (MIT/zlib), stb_image (public domain), шрифты IBM Plex (OFL)."));
#if defined(__ANDROID__)
            ImGui::Dummy({0, ui.S(4)});
            if (ui::LinkButton(ui, "##privacy", Tr("Политика конфиденциальности")))
                platform::OpenUrl("https://mikeverx.github.io/breezecad/privacy.html");
#endif
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::Dummy({0, ui.S(8)});
        if (ui::SecondaryButton(ui, "##about-close", Tr("Закрыть"), "Esc", {ui.S(140), ui.S(34)}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.shell.showAbout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace bz::app
