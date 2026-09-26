#include "SelectionRect.h"
#include "occt/OcctView.h"

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <algorithm>

namespace BreezeCAD::UI::Widgets {

void DrawSelectionRect(OcctView& view, GLFWwindow* window) {
    if (!view.IsSelectionRectActive()) return;
    if (ImGui::GetIO().WantCaptureMouse) return;

    int x1, y1, x2, y2;
    view.GetSelectionRect(x1, y1, x2, y2);

    // Координаты в framebuffer'е — переводим в логические (ImGui)
    int winW = 1, winH = 1, fbW = 1, fbH = 1;
    glfwGetWindowSize(window, &winW, &winH);
    glfwGetFramebufferSize(window, &fbW, &fbH);

    float sx = (fbW > 0) ? (float)winW / fbW : 1.0f;
    float sy = (fbH > 0) ? (float)winH / fbH : 1.0f;

    ImVec2 p1(x1 * sx, y1 * sy);
    ImVec2 p2(x2 * sx, y2 * sy);

    ImVec2 tl(std::min(p1.x, p2.x), std::min(p1.y, p2.y));
    ImVec2 br(std::max(p1.x, p2.x), std::max(p1.y, p2.y));

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    draw->AddRectFilled(tl, br, IM_COL32(80, 130, 180, 60));
    draw->AddRect(tl, br, IM_COL32(80, 130, 180, 255), 0.0f, 0, 1.5f);
}


} // namespace BreezeCAD::UI::Widgets
