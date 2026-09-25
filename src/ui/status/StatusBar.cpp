#include "StatusBar.h"

#include "imgui.h"

namespace BreezeCAD::UI::Status {

void DrawStatusBar(const StatusInfo& info) {
    ImGuiIO& io = ImGui::GetIO();
    const float h = 24.0f;

    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));

    ImGui::Begin("##statusbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPosY(4);

    // TODO: заменить зелёный хардкод на токен accent из темы
    if (info.sketchActive) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Эскиз");
    } else {
        ImGui::TextUnformatted("Режим ожидания");
    }

    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 16);

    if (info.sketchActive) {
        if (info.snapToGrid && info.snapToPoints) ImGui::TextDisabled("Снап: сетка + точки");
        else if (info.snapToGrid)                 ImGui::TextDisabled("Снап: сетка 10 мм");
        else if (info.snapToPoints)               ImGui::TextDisabled("Снап: точки");
        else                                       ImGui::TextDisabled("Снап: выкл");
    } else {
        ImGui::TextDisabled("Снап: н/д");
    }

    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 16);

    ImGui::TextDisabled("X: %.2f  Y: %.2f  Z: %.2f",
                        info.mouseX, info.mouseY, 0.0);

    const float rightPanelW = info.rightPanelExpanded ? 320.0f : 80.0f;
    const float margin      = 12.0f;

    const char* statusText = "Готово";
    const char* zoomText   = "1:1";

    float statusW = ImGui::CalcTextSize(statusText).x;
    float zoomW   = ImGui::CalcTextSize(zoomText).x;

    float statusX = io.DisplaySize.x - rightPanelW - margin - statusW;
    float zoomX   = statusX - 20.0f - zoomW;

    if (zoomX > 400.0f) {
        ImGui::SameLine(zoomX);
        ImGui::TextDisabled("%s", zoomText);
    }
    if (statusX > 400.0f) {
        ImGui::SameLine(statusX);
        ImGui::TextDisabled("%s", statusText);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace BreezeCAD::UI::Status
