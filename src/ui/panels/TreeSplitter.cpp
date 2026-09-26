#include "TreeSplitter.h"

#include "imgui.h"

namespace BreezeCAD::UI::Panels {

void DrawTreeSplitter(float& treeWidth) {
    ImGuiIO& io = ImGui::GetIO();

    const float splitterW = 5.0f;
    const float minTreeW  = 200.0f;
    const float maxTreeW  = 600.0f;

    const float topY    = 200.0f;
    const float bottomY = io.DisplaySize.y - 76.0f;
    const float xPos    = treeWidth;

    ImGui::SetNextWindowPos(ImVec2(xPos - splitterW * 0.5f, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(splitterW, bottomY - topY), ImGuiCond_Always);
    ImGui::Begin("##tree_splitter", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::InvisibleButton("##splitter_btn", ImVec2(splitterW, bottomY - topY));

    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (active) {
        treeWidth += io.MouseDelta.x;
        if (treeWidth < minTreeW) treeWidth = minTreeW;
        if (treeWidth > maxTreeW) treeWidth = maxTreeW;
    }

    if (active) {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddLine(ImVec2(xPos, topY), ImVec2(xPos, bottomY),
                      IM_COL32(80, 130, 180, 255), 2.0f);
    }

    ImGui::End();
}

} // namespace BreezeCAD::UI::Panels
