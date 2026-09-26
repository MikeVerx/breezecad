#include "ui/ribbon/RibbonTabs.h"

#include "app/IconManager.h"
#include "app/Localization.h"
#include "ui/widgets/SvgButton.h"

#include "imgui.h"

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

void DrawTabs(IconManager& icons, RibbonState& state) {
    static const char* tabs[] = {
        "tab.model", "tab.sketch", "tab.assembly", "tab.view"
    };
    for (int i = 0; i < 4; i++) {
        if (i > 0) ImGui::SameLine();
        bool active = (state.activeTab == i);
        bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
        if (active) {
            if (isLight) {
                // Светлая — серебристый C3
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.698f, 0.714f, 0.737f, 1.0f));
            } else {
                // Тёмная — синий accent
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        }
        if (ImGui::Button(Tr(tabs[i]).c_str(), ImVec2(120, 26))) {
            state.activeTab = i;
        }
        ImGui::PopStyleColor();
    }
    // Кнопка "+" для подключения модулей
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (UI::Widgets::SvgIconBtn(icons, "ribbon_plus", "plus", 16, 32, 26, "Подключить модуль")) {
        // TODO: диалог подключения модуля
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Подключить модуль");
}

}}}
