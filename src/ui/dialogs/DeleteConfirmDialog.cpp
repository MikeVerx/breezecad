#include "DeleteConfirmDialog.h"
#include "doc/BreezeDocument.h"

#include "imgui.h"

namespace BreezeCAD::UI::Dialogs {

void DrawDeleteConfirmDialog(const DeleteConfirmContext& ctx) {
    if (!ctx.state || !ctx.state->show) return;

    ImGui::OpenPopup("Подтверждение удаления");

    if (ImGui::BeginPopupModal("Подтверждение удаления", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", ctx.state->info);
        ImGui::Separator();
        ImGui::Checkbox("Не спрашивать снова (до перезапуска)",
                        &ctx.state->skipConfirm);
        ImGui::Separator();

        if (ImGui::Button("Удалить", ImVec2(120, 0))) {
            if (ctx.doc && !ctx.state->pendingLabel.IsNull()) {
                ctx.doc->RemoveFeature(ctx.state->pendingLabel);
                ctx.state->pendingLabel = TDF_Label();
                if (ctx.onDeleted) ctx.onDeleted();
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
