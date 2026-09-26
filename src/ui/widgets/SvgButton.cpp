#include "SvgButton.h"
#include "IconTint.h"
#include "app/IconManager.h"

#include "imgui.h"

namespace BreezeCAD::UI::Widgets {

bool SvgIconBtn(IconManager& icons, const char* id,
                       const char* name, int iconSize,
                       float btnW, float btnH,
                       const char* tooltip,
                       bool transparentBg) {
    ImGui::PushID(id);

    ImVec4 tint = UI::Widgets::IconTintColor(true);
    GLuint tex = icons.Get(name, iconSize);
    ImTextureID tid = (ImTextureID)(intptr_t)tex;

    ImGui::InvisibleButton("##svgb", ImVec2(btnW, btnH));
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!transparentBg) {
        ImU32 bg = ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        dl->AddRectFilled(min, max, bg, 4.0f);
    } else if (hovered) {
        dl->AddRectFilled(min, max,
            ImGui::GetColorU32(ImGuiCol_ButtonHovered), 4.0f);
    }

    if (tid != 0) {
        ImVec2 ip(min.x + (btnW - iconSize) * 0.5f,
                  min.y + (btnH - iconSize) * 0.5f);
        dl->AddImage(tid, ip,
                     ImVec2(ip.x + iconSize, ip.y + iconSize),
                     ImVec2(0, 0), ImVec2(1, 1),
                     ImGui::GetColorU32(tint));
    } else {
        dl->AddRectFilled(ImVec2(min.x + 4, min.y + 4),
                          ImVec2(max.x - 4, max.y - 4),
                          IM_COL32(140, 140, 140, 180), 3.0f);
    }

    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);

    ImGui::PopID();
    return clicked;
}

bool QatIconButton(IconManager& icons, const char* id,
                          const char* name, int size,
                          const char* tooltip) {
    return SvgIconBtn(icons, id, name, size, 32, 28, tooltip, true);
}








} // namespace BreezeCAD::UI::Widgets
