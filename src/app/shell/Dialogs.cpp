// Windows on top of the work: "save changes?", "МЦХ" and "about". All close
// with Esc.

#include "app/Format.h"
#include "app/Materials.h"
#include "app/shell/Shell.h"
#include "document/Commands.h"
#include "ui/Widgets.h"

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
