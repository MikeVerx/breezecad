// Windows on top of the work: "save changes?", "МЦХ" and "about". All close
// with Esc.

#include "app/Format.h"
#include "app/Language.h"
#include "app/Materials.h"
#include "app/Operations.h"
#include "app/ShortcutPresets.h"
#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "platform/FileDialogs.h"
#include "platform/Launch.h"
#include "ui/Widgets.h"

#include <array>
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
    }
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(Tr("Короткий щелчок ПКМ открывает меню, короткий щелчок колесом подтверждает операцию — при любом наборе."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

/// "Настройки": interface scale and theme; keyboard shortcuts.
void DrawSettings(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    if (!BeginDialog(ui, "##settings", app.shell.showSettings, ui.S(620))) return;
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(Tr("Настройки"));
    }
    const float width = ImGui::GetContentRegionAvail().x;
    static constexpr std::array<const char*, 4> tabs{N_("Интерфейс"), N_("Клавиши"), N_("Мышь"), N_("Общие")};
    app.shell.settingsTab = ui::Segmented(ui, "settings-tab", tabs, app.shell.settingsTab, 4, width);
    ImGui::Dummy({0, ui.S(6)});

    if (app.shell.settingsTab == 0) {
        DrawLanguageSettings(app, width);
        ImGui::Dummy({0, ui.S(6)});
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
        ui::FieldLabel(ui, Tr("Камера"));
        static constexpr std::array<const char*, 2> anim{N_("Плавные повороты"), N_("Без анимации")};
        const int a = app.settings.animations ? 0 : 1;
        if (const int n = ui::Segmented(ui, "anim", anim, a, 2, width); n != a) app.settings.animations = n == 0;
        ImGui::Dummy({0, ui.S(6)});
        ui::FieldLabel(ui, Tr("Оси в углу вида"));
        static constexpr std::array<const char*, 2> triad{N_("Показывать"), N_("Скрыть")};
        const int t = app.settings.showTriad ? 0 : 1;
        if (const int n = ui::Segmented(ui, "triad", triad, t, 2, width); n != t) app.settings.showTriad = n == 0;
        ImGui::Dummy({0, ui.S(6)});
        ui::FieldLabel(ui, Tr("Фон вида"));
        static constexpr std::array<const char*, 3> backgrounds{N_("Градиент"), N_("Светлый ровный"), N_("Тёмный ровный")};
        if (const int n = ui::Segmented(ui, "background", backgrounds, app.settings.viewBackground, 3, width); n != app.settings.viewBackground)
            app.settings.viewBackground = n;

    } else if (app.shell.settingsTab == 2) {
        DrawMouseSettings(app, width);
    } else if (app.shell.settingsTab == 3) {
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
    } else {
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
    ImGui::Dummy({0, ui.S(8)});
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape) && app.shell.captureShortcut.empty();
    if (ui::PrimaryButton(ui, "##settings-close", Tr("Готово"), nullptr, {ui.S(140), ui.S(34)}) || escape) {
        app.shell.showSettings = false;
        app.shell.captureShortcut.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

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

    DrawSettings(app);

    // --- About ---
    if (BeginDialog(ui, "##about", app.shell.showAbout, ui.S(480))) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ui.icons.Draw(dl, "brand-mark", p, ui.S(48), {1, 1, 1, 1});
        dl->AddText(ui.fonts.display, ui.S(24), {p.x + ui.S(62), p.y + ui.S(2)}, ui::ToU32(c.text), "BreezeCAD 0.2.0");
        dl->AddText(ui.fonts.body, ui.S(13), {p.x + ui.S(63), p.y + ui.S(32)}, ui::ToU32(c.textMuted),
                    Tr("Свободный параметрический САПР"));
        ImGui::Dummy({0, ui.S(60)});
        {
            ui::FontScope font(ui.fonts.bodySmall);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textSecondary));
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(Tr("Работает полностью офлайн. Никакие данные никуда не отправляются."));
            ImGui::TextUnformatted(Tr("Лицензия MPL-2.0. Файлы .bzc — открытый формат (ZIP + JSON)."));
            ImGui::Dummy({0, ui.S(4)});
            ImGui::TextUnformatted(Tr("Использует: Open CASCADE Technology (LGPL 2.1), Dear ImGui, GLFW, lunasvg, "
                                   "nlohmann/json, miniz, portable-file-dialogs (MIT/zlib), шрифты IBM Plex (OFL)."));
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
