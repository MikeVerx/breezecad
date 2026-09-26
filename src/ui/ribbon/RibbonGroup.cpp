#include "ui/ribbon/RibbonGroup.h"

#include "imgui.h"

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

void GroupStart(RibbonState& state) {
    state.groupStartY = ImGui::GetCursorScreenPos().y;
    ImGui::BeginGroup();
}

void GroupEnd(const char* name, RibbonState& state) {
    // Подтягиваем группу до общей высоты TARGET_H
    float curY = ImGui::GetCursorScreenPos().y;
    float groupH = curY - state.groupStartY;
    const float TARGET_H = 76.0f;
    if (groupH < TARGET_H) {
        ImGui::Dummy(ImVec2(1.0f, TARGET_H - groupH));
    }
    ImGui::EndGroup();

    ImVec2 pMin = ImGui::GetItemRectMin();
    ImVec2 pMax = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);

    // Фон группы — включает кнопки И подпись
    ImU32 bg = isLight ? IM_COL32(255, 255, 255, 60)
                       : IM_COL32(255, 255, 255, 12);
    dl->AddRectFilled(
        ImVec2(pMin.x - 6, pMin.y - 4),
        ImVec2(pMax.x + 6, pMax.y + 18),
        bg, 4.0f);

    // Вертикальный разделитель справа от группы
    dl->AddLine(
        ImVec2(pMax.x + 10, pMin.y - 2),
        ImVec2(pMax.x + 10, pMax.y + 16),
        isLight ? IM_COL32(160, 160, 160, 140) : IM_COL32(90, 100, 115, 180),
        1.0f);

    // Заголовок группы — снизу по центру (без изменения layout)
    ImVec2 tsz = ImGui::CalcTextSize(name);
    float cx = (pMin.x + pMax.x) * 0.5f - tsz.x * 0.5f;
    ImU32 tcol = isLight ? IM_COL32(90, 90, 90, 230)
                         : IM_COL32(180, 185, 195, 220);
    dl->AddText(ImVec2(cx, pMax.y + 3), tcol, name);

    // Продолжаем на той же строке
    ImGui::SameLine(0, 22);
}

}}}
