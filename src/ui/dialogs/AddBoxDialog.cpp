#include "AddBoxDialog.h"
#include "doc/BreezeDocument.h"
#include "app/Localization.h"

#include "imgui.h"

namespace BreezeCAD::UI::Dialogs {

void DrawAddBoxDialog(const AddBoxContext& ctx) {
    if (!ctx.state) return;

    if (ctx.state->show) {
        ImGui::OpenPopup(Tr("dlg.addBox.title").c_str());
        ctx.state->show = false;
    }
    if (ImGui::BeginPopupModal(Tr("dlg.addBox.title").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(Tr("dlg.name").c_str(), ctx.state->name,
                         sizeof(ctx.state->name));
        ImGui::Separator();
        ImGui::Text("%s", Tr("dlg.position").c_str());
        ImGui::InputFloat("X##pos", &ctx.state->pos[0]);
        ImGui::InputFloat("Y##pos", &ctx.state->pos[1]);
        ImGui::InputFloat("Z##pos", &ctx.state->pos[2]);
        ImGui::Separator();
        ImGui::Text("%s", Tr("dlg.size").c_str());
        ImGui::InputFloat("DX", &ctx.state->size[0]);
        ImGui::InputFloat("DY", &ctx.state->size[1]);
        ImGui::InputFloat("DZ", &ctx.state->size[2]);
        ImGui::Separator();
        if (ImGui::Button(Tr("btn.ok").c_str(), ImVec2(120, 0))) {
            if (ctx.doc) {
                ctx.doc->AddBoxFeature(ctx.state->name,
                    ctx.state->size[0], ctx.state->size[1], ctx.state->size[2],
                    ctx.state->pos[0],  ctx.state->pos[1],  ctx.state->pos[2]);
                if (ctx.onBoxAdded) ctx.onBoxAdded();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("btn.cancel").c_str(), ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace BreezeCAD::UI::Dialogs
