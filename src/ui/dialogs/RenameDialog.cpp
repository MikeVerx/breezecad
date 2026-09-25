#include "RenameDialog.h"
#include "doc/BreezeDocument.h"

#include "imgui.h"

namespace BreezeCAD::UI::Dialogs {

void DrawRenameDialog(const RenameContext& ctx) {
    if (!ctx.state || !ctx.state->show) return;

    ImGui::OpenPopup("Переименовать");

    if (ImGui::BeginPopupModal("Переименовать", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Новое имя:");
        ImGui::SetNextItemWidth(280);
        ImGui::InputText("##rename_buf", ctx.state->buffer,
                         sizeof(ctx.state->buffer),
                         ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (ctx.doc && !ctx.state->pendingLabel.IsNull() && ctx.state->buffer[0] != '\0') {
                ctx.doc->RenameFeature(ctx.state->pendingLabel, ctx.state->buffer);
                ctx.state->pendingLabel = TDF_Label();
            }
            ctx.state->show = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            ctx.state->pendingLabel = TDF_Label();
            ctx.state->show = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace BreezeCAD::UI::Dialogs
