#include "ProjectTabs.h"

#include "app/IconManager.h"
#include "ui/widgets/SvgButton.h"

#include "imgui.h"

#include <string>

namespace BreezeCAD::UI::Status {

void DrawProjectTabs(const ProjectTabsContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();

    const float statusH = 24.0f;
    const float tabsH   = 52.0f;
    const float yPos    = io.DisplaySize.y - statusH - tabsH;

    ImGui::SetNextWindowPos(ImVec2(0, yPos), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, tabsH), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::Begin("##project_tabs", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::BeginChild("##tabs_scroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0.0f) {
        float sx = ImGui::GetScrollX() - io.MouseWheel * 80.0f;
        if (sx < 0.0f) sx = 0.0f;
        ImGui::SetScrollX(sx);
    }

    // === Вкладка "Главная" ===
    {
        const bool onHome = ctx.onHome;
        if (onHome) {
            ImGui::BeginDisabled();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
        }
        if (ctx.icons && UI::Widgets::SvgIconBtn(*ctx.icons, "tab_home", "house", 14, 120, 30, "На главную")) {
            if (ctx.onGoHome) ctx.onGoHome();
        }
        if (onHome) {
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
        }
    }

    static int pendingCloseIdx = -1;
    static int switchToIdx    = -2;

    const int docCount = ctx.documentCount;
    for (int i = 0; i < docCount; i++) {
        ImGui::SameLine();
        ImGui::PushID(i);

        bool active = (i == ctx.activeIndex);
        ImVec4 accent(0.20f, 0.55f, 0.95f, 1.0f);
        ImVec4 accentHover(0.30f, 0.65f, 1.00f, 1.0f);
        ImVec4 accentActive(0.15f, 0.45f, 0.85f, 1.0f);

        ImVec4 colNormal = active ? accent : ImGui::GetStyleColorVec4(ImGuiCol_Button);
        ImVec4 colHover  = active ? accentHover : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        ImVec4 colActive = active ? accentActive : ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);

        ImGui::PushStyleColor(ImGuiCol_Button, colNormal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colActive);

        std::string label = (ctx.names && i < (int)ctx.names->size()) ? (*ctx.names)[i] : "";
        bool unsaved = (ctx.paths && i < (int)ctx.paths->size() && (*ctx.paths)[i].empty());
        if (unsaved) label += " *";

        if (ImGui::Button((label + "##tab").c_str(), ImVec2(140, 30))) {
            switchToIdx = i;
        }

        if (ImGui::BeginPopupContextItem("##tab_ctx")) {
            if (ImGui::MenuItem("Сохранить")) {}
            if (ImGui::MenuItem("Сохранить как...")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Закрыть")) pendingCloseIdx = i;
            if (ImGui::MenuItem("Закрыть другие")) {}
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(3);

        if (docCount > 1) {
            ImGui::SameLine(0, 2);
            if (ctx.icons && UI::Widgets::SvgIconBtn(*ctx.icons, "tab_close", "x", 12, 20, 30, "Закрыть")) {
                pendingCloseIdx = i;
            }
        }

        ImGui::PopID();
    }

    ImGui::SameLine();
    if (ctx.icons && UI::Widgets::SvgIconBtn(*ctx.icons, "tab_plus", "plus", 14, 28, 30, "Новый проект")) {
        if (ctx.onCreate) ctx.onCreate();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Новый проект");

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar();

    if (pendingCloseIdx >= 0) {
        if (ctx.onClose) ctx.onClose(pendingCloseIdx);
        pendingCloseIdx = -1;
    }
    if (switchToIdx != -2) {
        if (switchToIdx >= 0 && ctx.onSwitch) ctx.onSwitch(switchToIdx);
        switchToIdx = -2;
    }
}

} // namespace BreezeCAD::UI::Status
