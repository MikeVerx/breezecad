// Windows on top of the work: "save changes?", "МЦХ" and "about". All close
// with Esc.

#include "app/Format.h"
#include "app/Materials.h"
#include "app/Operations.h"
#include "app/ShortcutPresets.h"
#include "app/shell/Shell.h"
#include "document/Commands.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>
#include <format>
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
        ImGui::TextUnformatted("Массово-центровочные характеристики");
    }
    ImGui::Dummy({0, ui.S(4)});

    // Material: one of the known ones gives the density.
    ui::FieldLabel(ui, "Материал");
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
        mass = grams >= 1000.0 ? FormatGrouped(grams / 1000.0, 3) + " кг" : FormatGrouped(grams, 2) + " г";
    }
    ValueRow(ui, "Масса", mass);
    if (!material) {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::TextUnformatted(s->doc.info.material.empty() ? "Выберите материал, чтобы узнать массу."
                                                            : "Плотность этого материала неизвестна — выберите из списка.");
        ImGui::PopStyleColor();
    }
    ui::Divider(ui, c.dividerSoft);
    ValueRow(ui, "Объём", FormatGrouped(props->volume * k * k * k, 2) + " " + unit + "\xC2\xB3");
    ValueRow(ui, "Площадь поверхности", FormatGrouped(props->area * k * k, 2) + " " + unit + "\xC2\xB2");
    ui::Divider(ui, c.dividerSoft);
    const auto length = [&](double mm) { return FormatValue(mm, Quantity::Length, units, 3); };
    ValueRow(ui, "Центр масс X", length(props->centre.x) + " " + unit);
    ValueRow(ui, "Центр масс Y", length(props->centre.y) + " " + unit);
    ValueRow(ui, "Центр масс Z", length(props->centre.z) + " " + unit);
    ui::Divider(ui, c.dividerSoft);
    const Vec3 size = props->bounds.max - props->bounds.min;
    ValueRow(ui, "Габариты X \xC3\x97 Y \xC3\x97 Z",
             length(size.x) + " \xC3\x97 " + length(size.y) + " \xC3\x97 " + length(size.z) + " " + unit);

    ImGui::Dummy({0, ui.S(8)});
    const float bw = (ImGui::GetContentRegionAvail().x - ui.S(8)) / 2;
    if (ui::SecondaryButton(ui, "##mass-copy", "Копировать", nullptr, {bw, ui.S(36)})) {
        const std::string text = std::format(
            "Материал: {}\nМасса: {}\nОбъём: {} {}3\nПлощадь: {} {}2\nЦентр масс: {}; {}; {} {}\nГабариты: {} x {} x {} {}\n",
            material ? std::string(material->name) : std::string("не задан"), mass, FormatGrouped(props->volume * k * k * k, 2), unit,
            FormatGrouped(props->area * k * k, 2), unit, length(props->centre.x), length(props->centre.y),
            length(props->centre.z), unit, length(size.x), length(size.y), length(size.z), unit);
        ImGui::SetClipboardText(text.c_str());
        app.notes.Info("Характеристики скопированы");
    }
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##mass-close", "Закрыть", "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
    ImGui::EndPopup();
}

} // namespace

/// Remembers the shortcuts that differ from the shipped ones.
void StoreShortcuts(AppContext& app) {
    app.settings.shortcuts.clear();
    for (const Action& a : app.actions.All())
        if (a.chord != a.defaultChord) app.settings.shortcuts.emplace_back(a.id, ActionRegistry::ChordText(a.chord));
}

/// "Настройки": interface scale and theme; keyboard shortcuts.
void DrawSettings(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    if (!BeginDialog(ui, "##settings", app.shell.showSettings, ui.S(620))) return;
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted("Настройки");
    }
    const float width = ImGui::GetContentRegionAvail().x;
    static constexpr std::array<const char*, 2> tabs{"Интерфейс", "Клавиши"};
    app.shell.settingsTab = ui::Segmented(ui, "settings-tab", tabs, app.shell.settingsTab, 2, width);
    ImGui::Dummy({0, ui.S(6)});

    if (app.shell.settingsTab == 0) {
        ui::FieldLabel(ui, "Масштаб интерфейса");
        static constexpr std::array<const char*, 10> scales{"Авто", "75%", "85%", "90%", "100%", "110%", "125%", "150%", "175%", "200%"};
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
            ImGui::Text("Сейчас %.0f%%. «Авто» берёт масштаб экрана из системы и уменьшает интерфейс на небольших "
                        "экранах (1366×768), чтобы всё помещалось.", ui.scale * 100.0);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::Dummy({0, ui.S(6)});
        ui::FieldLabel(ui, "Оформление");
        static constexpr std::array<const char*, 3> themes{"Как в системе", "Светлая", "Тёмная"};
        const int theme = static_cast<int>(app.settings.theme);
        if (const int t = ui::Segmented(ui, "theme", themes, theme, 3, width); t != theme) SetTheme(app, static_cast<ThemeChoice>(t));
        ImGui::Dummy({0, ui.S(6)});
        ui::FieldLabel(ui, "Камера");
        static constexpr std::array<const char*, 2> anim{"Плавные повороты", "Без анимации"};
        const int a = app.settings.animations ? 0 : 1;
        if (const int n = ui::Segmented(ui, "anim", anim, a, 2, width); n != a) app.settings.animations = n == 0;
    } else {
        // The set of shortcuts: a ready-made one, or "Свой" once changed by hand.
        ui::FieldLabel(ui, "Набор сочетаний");
        const ShortcutPreset* base = FindShortcutPreset(app.settings.shortcutPreset);
        if (!base) base = &ShortcutPresets().front();
        const std::string shown = app.settings.shortcutsCustom ? "Свой (на основе: " + std::string(base->label) + ")"
                                                               : std::string(base->label);
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##preset", shown.c_str())) {
            for (const ShortcutPreset& p : ShortcutPresets())
                if (ImGui::Selectable(std::string(p.label).c_str(), !app.settings.shortcutsCustom && &p == base)) {
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
            ImGui::TextUnformatted("Наборы повторяют привычные клавиши других программ; их названия принадлежат правообладателям.");
            ImGui::PopStyleColor();
        }
        ImGui::Dummy({0, ui.S(4)});
        ui::TextField(ui, "##shortcut-filter", app.shell.shortcutFilter, width, "Поиск команды");
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
                        app.shell.shortcutNote = ActionRegistry::ChordText(value) + " было у «" + other->label + "» — снято оттуда";
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
            if (!filter.empty() && a.label.find(filter) == std::string::npos) continue;
            ImGui::PushID(a.id.c_str());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x, h = ui.S(30);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (!a.icon.empty()) ui.icons.Draw(dl, a.icon, {p.x + ui.S(6), p.y + ui.S(6)}, ui.S(18), c.textSecondary);
            dl->AddText(ui.fonts.body, ui.S(13), {p.x + ui.S(32), p.y + ui.S(8)}, ui::ToU32(c.text), a.label.c_str());
            const bool capturing = app.shell.captureShortcut == a.id;
            const std::string chord = capturing ? std::string("Нажмите сочетание…") : a.chord ? ActionRegistry::ChordText(a.chord) : "—";
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
            ImGui::TextUnformatted("Щёлкните сочетание и нажмите новое. Esc — отмена, Backspace — убрать. Точка — изменено.");
            ImGui::PopStyleColor();
        }
        if (ui::SecondaryButton(ui, "##reset-keys", "Сбросить все сочетания", nullptr, {ui.S(220), ui.S(32)})) {
            app.actions.ResetChords();
            app.settings.shortcutPreset = "breezecad";
            app.settings.shortcutsCustom = false;
            StoreShortcuts(app);
            app.shell.shortcutNote.clear();
        }
    }
    ImGui::Dummy({0, ui.S(8)});
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape) && app.shell.captureShortcut.empty();
    if (ui::PrimaryButton(ui, "##settings-close", "Готово", nullptr, {ui.S(140), ui.S(34)}) || escape) {
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

    DrawMassProperties(app);

    DrawSettings(app);

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
