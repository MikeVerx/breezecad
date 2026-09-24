#include "Tooltip.h"

#include "imgui.h"

namespace BreezeCAD::UI::Widgets {

void TooltipState::Draw(const std::string& id,
                                       const std::string& name,
                                       const std::string& shortDesc,
                                       const std::string& longDesc,
                                       const std::string& imagePath) {
    // Вызывается сразу после кнопки. Проверяем — наведена ли она.
    if (!ImGui::IsItemHovered()) {
        // Если это тот же элемент, что сейчас в hover — сбрасываем
        if (myHoveredItemId == id) {
            myHoveredItemId.clear();
            myHoverStartTime = 0.0;
            myLastTooltipLevel = 0;
        }
        return;
    }

    // Только что навели на новый элемент
    if (myHoveredItemId != id) {
        myHoveredItemId = id;
        myHoverStartTime = ImGui::GetTime();
        myLastTooltipLevel = 0;
    }

    double elapsed = ImGui::GetTime() - myHoverStartTime;

    int level = 0;
    if (elapsed >= 3.0)      level = 3;
    else if (elapsed >= 1.5) level = 2;
    else if (elapsed >= 0.5) level = 1;
    else                     return;  // ещё рано

    if (level == 0) return;

    // Рисуем tooltip
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);

    // Уровень 1: название
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", name.c_str());

    // Уровень 2: краткое описание
    if (level >= 2 && !shortDesc.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(shortDesc.c_str());
    }

    // Уровень 3: подробное описание + картинка
    if (level >= 3) {
        if (!longDesc.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", longDesc.c_str());
        }
        if (!imagePath.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("(картинка: %s)", imagePath.c_str());
            // TODO: загрузка PNG через imgui — следующая итерация
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}


} // namespace BreezeCAD::UI::Widgets
