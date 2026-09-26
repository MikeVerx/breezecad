#include "ui/ribbon/tabs/ToolsTab.h"

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

void DrawToolsTab(IconManager& icons, RibbonState& /*state*/) {
    // --- Измерить ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.measure").c_str());
    SingleButton(icons, "measure",  "tool.measure",  ICON_MEASURE,  true, false);
    ImGui::SameLine();
    SingleButton(icons, "distance", "tool.distance", ICON_DISTANCE, true, false);
    ImGui::SameLine();
    SingleButton(icons, "angle",    "tool.angle",    ICON_ANGLE,    true, false);
    ImGui::SameLine();
    SingleButton(icons, "area",     "tool.area",     ICON_AREA,     true, false);
    ImGui::SameLine();
    SingleButton(icons, "volume",   "tool.volume",   ICON_VOLUME,   true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Анализ ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.analysis").c_str());
    SingleButton(icons, "section",        "tool.section",        ICON_SECTION,        true, false);
    ImGui::SameLine();
    SingleButton(icons, "draft_analysis", "tool.draft_analysis", ICON_DRAFT_ANALYSIS, true, false);
    ImGui::SameLine();
    SingleButton(icons, "thickness",      "tool.thickness",      ICON_THICKNESS,      true, false);
    ImGui::SameLine();
    SingleButton(icons, "curvature",      "tool.curvature",      ICON_CURVATURE,      true, false);
    ImGui::SameLine();
    SingleButton(icons, "zebra",          "tool.zebra",          ICON_ZEBRA,          true, false);
    ImGui::SameLine();
    SingleButton(icons, "interference",   "tool.interference",   ICON_INTERFERENCE,   true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Свойства ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.properties").c_str());
    SingleButton(icons, "model_props", "tool.model_props", ICON_MODEL_PROPS, true, false);
    ImGui::SameLine();
    SingleButton(icons, "mass",        "tool.mass",        ICON_MASS,        true, false);
    ImGui::EndGroup();
}

}}}
