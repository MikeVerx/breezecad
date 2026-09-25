#include "NotImplementedDialog.h"

#include "imgui.h"

namespace BreezeCAD::UI::Dialogs {

void DrawNotImplementedDialog(NotImplementedState& state) {
    if (state.show) {
        ImGui::OpenPopup("##not_implemented");
    }
    if (ImGui::BeginPopupModal("##not_implemented", &state.show,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Функция \"%s\" не разработана.", state.feature.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Будет доступно в следующих версиях BreezeCAD.");
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            state.show = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace BreezeCAD::UI::Dialogs
