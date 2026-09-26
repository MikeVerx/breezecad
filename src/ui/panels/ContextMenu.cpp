#include "ContextMenu.h"
#include "doc/BreezeDocument.h"
#include "occt/OcctView.h"
#include "util/Utf8.h"

#include "imgui.h"
#include <TDataStd_Name.hxx>
#include <gp_Dir.hxx>

namespace BreezeCAD::UI::Panels {

void DrawContextMenu3D(const TreeContext& ctx) {
    if (!ctx.pendingContextMenu || !ctx.contextMenuPos) return;

    ImVec2* pos = (ImVec2*)ctx.contextMenuPos;

    if (*ctx.pendingContextMenu) {
        ImGui::SetNextWindowPos(*pos, ImGuiCond_Always);
        ImGui::OpenPopup("##ctx_3d");
        *ctx.pendingContextMenu = false;
    }

    if (ImGui::BeginPopup("##ctx_3d")) {
        TDF_Label sel;
        if (ctx.view) sel = ctx.view->GetSelectedLabel();

        if (sel.IsNull()) {
            if (ImGui::MenuItem("Домой (F6)")) {
                if (ctx.view) ctx.view->SetHomeView();
            }
            if (ImGui::MenuItem("Изометрия")) {
                if (ctx.view) ctx.view->SetHomeView();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Вид")) {
                if (ImGui::MenuItem("Сверху"))
                    ctx.view->SetCameraDirectionAndUp(gp_Dir(0, 0, -1), gp_Dir(0, 1, 0));
                if (ImGui::MenuItem("Снизу"))
                    ctx.view->SetCameraDirectionAndUp(gp_Dir(0, 0, 1), gp_Dir(0, 1, 0));
                if (ImGui::MenuItem("Спереди"))
                    ctx.view->SetCameraDirectionAndUp(gp_Dir(0, -1, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Сзади"))
                    ctx.view->SetCameraDirectionAndUp(gp_Dir(0, 1, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Справа"))
                    ctx.view->SetCameraDirectionAndUp(gp_Dir(-1, 0, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Слева"))
                    ctx.view->SetCameraDirectionAndUp(gp_Dir(1, 0, 0), gp_Dir(0, 0, 1));
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Показать всё")) {
                if (ctx.view) ctx.view->FitAll();
            }
        } else {
            std::string name = "<unnamed>";
            Handle(TDataStd_Name) nameAttr;
            if (sel.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
                name = Util::ExtToUtf8(nameAttr->Get());
            }
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();

            bool hasOps = ctx.doc ? ctx.doc->Has3DOperations() : false;
            DrawContextMenuItems(ctx, sel, hasOps);
        }
        ImGui::EndPopup();
    }
}

} // namespace BreezeCAD::UI::Panels
