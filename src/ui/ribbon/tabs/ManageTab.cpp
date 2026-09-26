#include "ui/ribbon/tabs/ManageTab.h"

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

void DrawManageTab(IconManager& icons, RibbonState& /*state*/) {
    // --- Параметры ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.parameters").c_str());
    SingleButton(icons, "parameters", "tool.parameters", ICON_PARAMETERS, true, true);
    ImGui::SameLine();
    SingleButton(icons, "functions",  "tool.functions",  ICON_FUNCTION,   true, true);
    ImGui::SameLine();
    SingleButton(icons, "texts",      "tool.texts",      ICON_TEXT,       true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Материалы ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.materials").c_str());
    SingleButton(icons, "material",   "tool.material",   ICON_MATERIAL,   true, false);
    ImGui::SameLine();
    SingleButton(icons, "texture",    "tool.texture",    ICON_TEXTURE,    true, false);
    ImGui::SameLine();
    SingleButton(icons, "appearance", "tool.appearance", ICON_APPEARANCE, true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Обновление ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.update").c_str());
    SingleButton(icons, "update",    "tool.update",    ICON_UPDATE,    true, false);
    ImGui::SameLine();
    SingleButton(icons, "recompute", "tool.recompute", ICON_RECOMPUTE, true, false);
    ImGui::SameLine();
    SingleButton(icons, "report",    "tool.report",    ICON_REPORT,    true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Вставка ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.insert").c_str());
    SingleButton(icons, "insert_obj", "tool.insert_obj", ICON_INSERT_OBJ, true, false);
    ImGui::SameLine();
    SingleButton(icons, "insert_img", "tool.insert_img", ICON_INSERT_IMG, true, false);
    ImGui::SameLine();
    SingleButton(icons, "import",     "tool.import",     ICON_IMPORT,     true, false);
    ImGui::EndGroup();
}

}}}
