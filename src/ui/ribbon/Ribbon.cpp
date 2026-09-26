#include "ui/ribbon/Ribbon.h"
#include "ui/ribbon/RibbonTabs.h"
#include "ui/ribbon/tabs/ModelTab.h"
#include "ui/ribbon/tabs/ViewTab.h"

#include "imgui.h"

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

void Draw(RibbonContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 206), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    ImGui::Begin("##ribbon", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    if (ctx.drawQat) ctx.drawQat();
    ImGui::Separator();

    if (ctx.icons && ctx.state) UI::Ribbon::DrawTabs(*ctx.icons, *ctx.state);
    ImGui::Separator();

    // Child с отступом сверху 6 px
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    if (ImGui::BeginChild("##ribbon_content", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // Колесо мыши прокручивает ribbon горизонтально
        ImGuiIO& ioChild = ImGui::GetIO();
        if (ioChild.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            float scrollX = ImGui::GetScrollX() - ioChild.MouseWheel * 60.0f;
            if (scrollX < 0.0f) scrollX = 0.0f;
            ImGui::SetScrollX(scrollX);
        }
        // Switch по активной вкладке. Model/View уже в модулях, остальные — колбэки.
        switch (ctx.state->activeTab) {
            case 0: UI::Ribbon::DrawModelTab(*ctx.icons, *ctx.state); break;
            case 1: if (ctx.drawSketchTab)   ctx.drawSketchTab();     break;
            case 2: if (ctx.drawAssemblyTab) ctx.drawAssemblyTab();   break;
            case 3: UI::Ribbon::DrawViewTab(*ctx.icons, *ctx.state);  break;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar();
}

}}}
