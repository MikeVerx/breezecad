// Windows on top of the work: "save changes?", "МЦХ" and "about". All close
// with Esc.

#include "app/BetaTesting.h"
#include "app/Format.h"
#include "app/Language.h"
#include "app/Materials.h"
#include "app/Operations.h"
#include "app/ShortcutPresets.h"
#include "app/Backgrounds.h"
#include "app/shell/Shell.h"
#include "app/shell/TouchpadSettings.h"
#include "core/I18n.h"
#include "core/Version.h"
#include "document/Commands.h"
#include "app/assembly/AssemblyUi.h"
#include "document/StandardParts.h"
#include "core/PathText.h"
#include "modules/ModuleManager.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Menus.h"
#include "platform/Launch.h"
#include "ui/Widgets.h"

#include <array>
#include <imgui_stdlib.h>
#include <cmath>
#include <format>
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>

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
    ShellState::Library::Pick p{std::string(s.id), {}, 0};
    const parts::StandardInfo* old = parts::FindStandard(from.standard);
    p.size = old ? parts::MatchingSize(*old, from.size, s) : parts::Sizes(s).front();
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
    LibraryHeading(ui, standard.kind == parts::PartKind::BallBearing    ? Tr("Подшипник")
                       : standard.kind == parts::PartKind::RingExternal ? Tr("Диаметр вала, мм")
                       : standard.kind == parts::PartKind::RingInternal ? Tr("Диаметр отверстия, мм")
                       : parts::HasThread(standard.kind)                ? Tr("Резьба")
                                                                        : Tr("Диаметр, мм"));
    {
        std::vector<const char*> labels;
        for (const std::string& s : sizes) labels.push_back(s.c_str());
        const int current = static_cast<int>(std::ranges::find(sizes, lib.chosen.size) - sizes.begin());
        if (const int chosen = ui::Segmented(ui, "size", labels, current, 6, width); chosen != current && chosen >= 0)
            lib.chosen.size = sizes[static_cast<std::size_t>(chosen)];
    }
    const std::vector<double> lengths = parts::Lengths(standard, lib.chosen.size);
    if (!lengths.empty()) {
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
}

/// "Выбор стандартного изделия": recent parts, the standards filter, the
/// folders on the left, the chosen part on the right; makes a new part.
void DrawLibrary(AppContext& app) {
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
                    if (static_cast<int>(parts::FamilyOf(*s)) == lib.family) items.push_back(s);
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
        app.notes.Info(Tr("Пакеты стандартных изделий появятся в следующих версиях"));

    // Right: the chosen part, or nothing.
    const parts::StandardInfo* standard = parts::FindStandard(lib.chosen.standard);
    ImGui::SameLine(0, gap);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (height - ui.S(40)) - ImGui::GetStyle().ItemSpacing.y);
    ImGui::BeginChild("##library-part", {0, height}, ImGuiChildFlags_None);
    if (standard) DrawLibraryPart(app, *standard, ImGui::GetContentRegionAvail().x - ui.S(8));
    ImGui::EndChild();

    // Footer.
    ui::Divider(ui, c.dividerSoft);
    standard = parts::FindStandard(lib.chosen.standard);
    const parts::Request request{lib.chosen.standard, lib.chosen.size, lib.chosen.length, lib.realThread, lib.appearance};
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
    ImGui::SetCursorPosY(rowY);
    ImGui::SetCursorPosX(rowX + rowW - 2 * bw - ui.S(8));
    if (ui::SecondaryButton(ui, "##library-cancel", Tr("Закрыть"), "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close();
    ImGui::SameLine(0, ui.S(8));
    // In an assembly the part goes into it (its file next to the assembly).
    if ((ui::PrimaryButton(ui, "##library-create", intoAssembly ? Tr("Вставить в сборку") : Tr("Создать деталь"), "Enter",
                           {bw, ui.S(36)}, standard != nullptr) ||
         (standard && ui::EnterPressed())) &&
        standard && (intoAssembly ? PlaceStandardPart(app, request) : CreateStandardPart(app, request)))
        close();
    ImGui::EndPopup();
}

} // namespace

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

void DrawSettings(AppContext& app) {
    ui::Ui& ui = app.ui;
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
        static constexpr std::array<const char*, 3> subs{N_("Клавиатура"), N_("Мышь"), N_("Тачпад")};
        app.shell.settingsSub = ui::Segmented(ui, "settings-sub", subs, app.shell.settingsSub, 3, width);
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
            else if (app.shell.settingsSub == 2) DrawTouchpadSettings(app, width);
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
        if (ui::PrimaryButton(ui, "##beta-show", Tr("Открыть папку с отчётом"), nullptr, {bw, ui.S(36)}))
            if (!platform::OpenFolder(r.packed.parent_path())) app.notes.Error(Tr("Не удалось открыть папку"));
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
        const std::string build = *Commit() ? Trf("Сборка {} {} от {}", Branch(), Commit(), BuildDate())
                                            : Trf("Сборка от {}", BuildDate());
        dl->AddText(ui.fonts.mono, ui.S(11), {p.x + ui.S(63), p.y + ui.S(52)}, ui::ToU32(c.textMuted), build.c_str());
        ImGui::Dummy({0, ui.S(74)});
        {
            ui::FontScope font(ui.fonts.bodySmall);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textSecondary));
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(Tr("Работает полностью офлайн. Никакие данные никуда не отправляются."));
            ImGui::TextUnformatted(Tr("Лицензия MPL-2.0. Файлы .bzc — открытый формат (ZIP + JSON)."));
            ImGui::Dummy({0, ui.S(4)});
            ImGui::TextUnformatted(Tr("Использует: Open CASCADE Technology (LGPL 2.1), Dear ImGui, GLFW, lunasvg, "
                                   "nlohmann/json, miniz, portable-file-dialogs (MIT/zlib), stb_image (public domain), шрифты IBM Plex (OFL)."));
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
