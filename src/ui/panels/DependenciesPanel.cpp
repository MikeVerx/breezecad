#include "DependenciesPanel.h"
#include "app/Localization.h"

#include "imgui.h"

namespace BreezeCAD::UI::Panels {

void DrawDependenciesPanel(bool sketchActive) {
    if (!sketchActive) return;

    ImGuiIO& io = ImGui::GetIO();
    const float panelW = 280.0f;
    const float panelTop = 340.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelW - 15, panelTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, 0), ImGuiCond_Always);

    ImGui::Begin(Tr("panel.dependencies").c_str(), nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextDisabled("%s", Tr("panel.deps.hint").c_str());

    if (ImGui::BeginListBox("##deps_list", ImVec2(-1, 120))) {
        ImGui::TextDisabled("%s", Tr("panel.deps.empty").c_str());
        ImGui::EndListBox();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Fully Constrained: NO");
    ImGui::TextDisabled("DOF remaining: ?");

    ImGui::End();
}

} // namespace BreezeCAD::UI::Panels
