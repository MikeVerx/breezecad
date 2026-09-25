#include "UnsavedChangesDialog.h"

#include "imgui.h"

namespace BreezeCAD::UI::Dialogs {

void DrawUnsavedChangesDialog(const UnsavedContext& ctx) {
    if (!ctx.state || !ctx.state->show) return;

    ImGui::OpenPopup("Несохранённые изменения");

    if (ImGui::BeginPopupModal("Несохранённые изменения", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", ctx.state->info);
        ImGui::Separator();

        if (ImGui::Button("Сохранить", ImVec2(120, 0))) {
            const int  idx       = ctx.state->pendingCloseDocIndex;
            const bool wasWindow = ctx.state->pendingWindowClose;

            bool saved = true;
            if (idx >= 0 && idx < ctx.documentCount && ctx.onSave) {
                saved = ctx.onSave(idx);
            }
            if (!saved) return;   // диалог остаётся открытым

            ctx.state->pendingCloseDocIndex = -1;
            ctx.state->pendingWindowClose   = false;
            ctx.state->show = false;
            ImGui::CloseCurrentPopup();

            if (wasWindow) {
                if (ctx.onQuit) ctx.onQuit();
            } else if (idx >= 0 && ctx.onCloseDoc) {
                ctx.onCloseDoc(idx);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Не сохранять", ImVec2(120, 0))) {
            const int  idx       = ctx.state->pendingCloseDocIndex;
            const bool wasWindow = ctx.state->pendingWindowClose;
            ctx.state->pendingCloseDocIndex = -1;
            ctx.state->pendingWindowClose   = false;
            ctx.state->show = false;
            ImGui::CloseCurrentPopup();

            if (idx >= 0 && idx < ctx.documentCount && ctx.onDiscard) {
                ctx.onDiscard(idx);
            }
            if (wasWindow) {
                if (ctx.onQuit) ctx.onQuit();
            } else if (idx >= 0 && idx < ctx.documentCount && ctx.onCloseDoc) {
                ctx.onCloseDoc(idx);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            ctx.state->pendingCloseDocIndex = -1;
            ctx.state->pendingWindowClose   = false;
            ctx.state->show = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace BreezeCAD::UI::Dialogs
