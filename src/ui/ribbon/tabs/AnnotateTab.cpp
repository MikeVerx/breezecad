#include "ui/ribbon/tabs/AnnotateTab.h"

#include "app/IconManager.h"
#include "app/IconsFontAwesome.h"
#include "app/Localization.h"
#include "ui/ribbon/RibbonButton.h"
#include "ui/ribbon/RibbonState.h"

#include "imgui.h"
#include "imgui_internal.h"   // SeparatorEx, ImGuiSeparatorFlags_Vertical

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

void DrawAnnotateTab(IconManager& icons, RibbonState& /*state*/) {
    // --- Размеры ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.dimensions").c_str());
    SingleButton(icons, "dim_linear",   "tool.dim_linear",   ICON_DIM_LINEAR,   true, true);
    ImGui::SameLine();
    SingleButton(icons, "dim_angle",    "tool.dim_angle",    ICON_DIM_ANGLE,    true, true);
    ImGui::SameLine();
    SingleButton(icons, "dim_radius",   "tool.dim_radius",   ICON_DIM_RADIUS,   true, true);
    ImGui::SameLine();
    SingleButton(icons, "dim_diameter", "tool.dim_diameter", ICON_DIM_DIAMETER, true, true);
    ImGui::SameLine();
    SingleButton(icons, "dim_ordinate", "tool.dim_ordinate", ICON_DIM_ORDINATE, true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Текст и символы ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.text").c_str());
    SingleButton(icons, "leader",    "tool.leader",    ICON_LEADER,    true, false);
    ImGui::SameLine();
    SingleButton(icons, "text_ann",  "tool.text",      ICON_TEXT,      true, false);
    ImGui::SameLine();
    SingleButton(icons, "datum",     "tool.datum",     ICON_DATUM,     true, false);
    ImGui::SameLine();
    SingleButton(icons, "tolerance", "tool.tolerance", ICON_TOLERANCE, true, false);
    ImGui::SameLine();
    SingleButton(icons, "roughness", "tool.roughness", ICON_ROUGHNESS, true, false);
    ImGui::SameLine();
    SingleButton(icons, "weld",      "tool.weld",      ICON_WELD,      true, false);
    ImGui::EndGroup();
}

}}}
