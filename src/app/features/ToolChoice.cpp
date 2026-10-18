#include "app/features/ToolChoice.h"

#include "app/AppContext.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <string>
#include <vector>

namespace bz::app {

int ToolChoice(AppContext& app, const char* id, const char* title, std::span<const ChoiceItem> items, int current, float width,
               int disabledMask, bool lineType) {
    ui::Ui& ui = app.ui;
    if (app.shell.mobile.phone) {
        // The phone: recorded for the island; a tap there comes back here next frame.
        mobile::MobileState& m = app.shell.mobile;
        mobile::MobileState::ChoiceGroup g;
        g.id = id;
        g.title = Tr(title);
        for (const ChoiceItem& it : items) g.items.emplace_back(it.icon ? it.icon : "", Tr(it.label));
        g.current = current;
        g.disabled = disabledMask;
        g.lineType = lineType;
        m.choices.push_back(std::move(g));
        if (m.pickedChoice == id) {
            const int picked = m.pickedIndex;
            m.pickedChoice.clear();
            m.pickedIndex = -1;
            if (picked >= 0 && picked < static_cast<int>(items.size()) && !(disabledMask & (1 << picked))) return picked;
        }
        return current;
    }

    const bool icons = std::all_of(items.begin(), items.end(), [](const ChoiceItem& it) { return it.icon != nullptr; });
    if (!icons) {
        // No pictures yet: the caption over the usual segmented strip.
        ui::FieldLabel(ui, Tr(title));
        ImGui::Dummy({0, ui.S(2)});
        std::vector<const char*> labels;
        labels.reserve(items.size());
        for (const ChoiceItem& it : items) labels.push_back(it.label);
        const int columns = items.size() > 3 ? 2 : static_cast<int>(items.size());
        const int chosen = ui::Segmented(ui, id, labels, current, columns, width, disabledMask);
        ImGui::Dummy({0, ui.S(10)});
        return chosen;
    }
    // KOMPAS-3D: the caption on the left, the buttons in a row on the right (under it when narrow).
    int chosen = current;
    const float button = ui.S(34), gap = ui.S(4);
    const float captionW = ui.S(96);
    const float buttonsW = static_cast<float>(items.size()) * (button + gap) - gap;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* caption = Tr(title);
    ImGui::PushFont(ui.fonts.body);
    const float fs = ImGui::GetFontSize();
    const float textW = ImGui::CalcTextSize(caption).x;
    ImGui::PopFont();
    // A long caption («Выравнивание в рамке») ran under the buttons: then they go under it.
    const bool beside = captionW + buttonsW <= width && textW <= captionW - ui.S(6);
    dl->AddText(ui.fonts.body, fs, {start.x, beside ? start.y + (button - fs) / 2 : start.y}, ui::ToU32(ui.C().textSecondary), caption);
    ImVec2 at{beside ? start.x + captionW : start.x, beside ? start.y : start.y + fs + ui.S(6)};
    const float rowStart = at.x;
    ImGui::PushID(id);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (at.x + button > start.x + width + 0.5f && at.x > rowStart) {
            at.x = rowStart;
            at.y += button + gap;
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::PushID(static_cast<int>(i));
        const bool enabled = !(disabledMask & (1 << i));
        const bool on = static_cast<int>(i) == current;
        // The mockup 8a: bordered squares; the chosen one tinted with an accent border; one that
        // cannot be chosen now pale (colour icons are not tinted by ImGui, so drawn by hand).
        const bool clicked = ImGui::InvisibleButton("##choice", {button, button});
        const bool hovered = ImGui::IsItemHovered();
        const ui::ThemeColors& c = ui.C();
        const ImVec2 b{at.x + button, at.y + button};
        dl->AddRectFilled(at, b, ui::ToU32(on ? c.accentSoft : hovered && enabled ? c.hover : c.field), ui.S(7));
        dl->AddRect(at, b, ui::ToU32(on ? c.accent : c.fieldBorder), ui.S(7), 0, ui.S(on ? 1.5f : 1.0f));
        const float is = ui.S(22);
        ui.icons.Draw(dl, items[i].icon, {at.x + (button - is) / 2, at.y + (button - is) / 2}, is,
                      enabled ? Color{1, 1, 1, 1} : Color{1, 1, 1, 0.3f});
        ui::ItemTooltip(ui, Tr(items[i].label));
        if (clicked && enabled) chosen = static_cast<int>(i);
        ImGui::PopID();
        at.x += button + gap;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos({start.x, at.y + button + ui.S(10)});
    ImGui::Dummy({width, 0});
    return chosen;
}

bool DeferCaption(AppContext& app, const char* text) {
    if (!app.shell.mobile.phone || !text || !*text) return false;
    app.shell.mobile.deferredCaptions.emplace_back(text);
    return true;
}

void FlushCaptions(AppContext& app, float width) {
    auto& kept = app.shell.mobile.deferredCaptions;
    if (kept.empty()) return;
    ui::Ui& ui = app.ui;
    ImGui::Dummy({0, ui.S(14)});
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, {p.x + width, p.y}, ui::ToU32(ui.C().dividerSoft));
    ImGui::Dummy({0, ui.S(6)});
    ui::FontScope font(ui.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    for (const std::string& t : kept) {
        ImGui::TextUnformatted(t.c_str());
        ImGui::Dummy({0, ui.S(3)});
    }
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    kept.clear();
}

} // namespace bz::app
