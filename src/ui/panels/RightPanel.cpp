#include "RightPanel.h"
#include "app/IconManager.h"
#include "occt/OcctView.h"
#include "ui/widgets/IconTint.h"
#include "ui/widgets/SvgButton.h"
#include "ui/widgets/Tooltip.h"

#include "imgui.h"

namespace BreezeCAD::UI::Panels {

void DrawRightPanel(const RightPanelContext& ctx) {
    if (!ctx.expanded || !ctx.icons) return;
    ImGuiIO& io = ImGui::GetIO();

    const float toolW    = 44.0f;
    const float toolH    = 34.0f;
    const float arrowH   = 20.0f;
    const float handleW  = 26.0f;
    const float pad      = 3.0f;
    const float panelW   = toolW + pad * 2.0f;
    bool forceCollapsed = (io.DisplaySize.x < 1100.0f);
    bool panelExpanded = *ctx.expanded && !forceCollapsed;

    const float topY     = 235.0f;
    const float panelH   = 330.0f;
    const float rightEdge = io.DisplaySize.x;

    const float edgeMargin = 14.0f;
    float handleX = panelExpanded
                    ? (rightEdge - panelW - handleW - edgeMargin)
                    : (rightEdge - handleW - edgeMargin);
    float handleH = 84.0f;
    float handleY = topY + (panelH - handleH) * 0.35f;

    ImGui::SetNextWindowPos(ImVec2(handleX, handleY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(handleW, handleH), ImGuiCond_Always);
    ImGui::Begin("##right_handle", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 hMin = wpos;
    ImVec2 hMax = ImVec2(wpos.x + handleW, wpos.y + handleH);

    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##right_handle_btn", ImVec2(handleW, handleH));
    bool hClicked = ImGui::IsItemClicked();
    bool hHovered = ImGui::IsItemHovered();

    ImDrawList* hdl = ImGui::GetWindowDrawList();

    ImU32 hBg;
    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
    if (hHovered) {
        hBg = IM_COL32(60, 120, 200, 255);
    } else if (isLight) {
        hBg = IM_COL32(140, 130, 110, 240);
    } else {
        hBg = IM_COL32(90, 100, 120, 240);
    }
    hdl->AddRectFilled(hMin, hMax, hBg, 6.0f);

    const char* hIcon = panelExpanded ? "chevron-right" : "chevron-left";
    GLuint hTex = ctx.icons->Get(hIcon, 20);
    if (hTex != 0) {
        ImVec2 ip(hMin.x + (handleW - 20) * 0.5f,
                  hMin.y + (handleH - 20) * 0.5f);
        hdl->AddImage((ImTextureID)(intptr_t)hTex, ip,
                      ImVec2(ip.x + 20, ip.y + 20),
                      ImVec2(0, 0), ImVec2(1, 1),
                      ImGui::GetColorU32(UI::Widgets::IconTintColor(true)));
    }

    if (hClicked) {
        *ctx.expanded = !*ctx.expanded;
    }
    if (hHovered) {
        ImGui::SetTooltip(panelExpanded ? "Свернуть панель" : "Развернуть панель");
    }
    ImGui::End();

    if (!panelExpanded) return;

    float panelX = rightEdge - panelW - edgeMargin;
    ImGui::SetNextWindowPos(ImVec2(panelX, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::Begin("##right_panel", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));

    // --- 1. Панорамировать ---
    if (UI::Widgets::SvgIconBtn(*ctx.icons, "panel_pan", "hand", 20, toolW, toolH, "Панорамировать")) {}
    if (ctx.tooltip) ctx.tooltip->Draw("panel.pan", "Панорамировать",
        "Сдвинуть вид мышью",
        "Активирует инструмент панорамирования. После активации зажмите ЛКМ "
        "в 3D-сцене и перемещайте мышь, чтобы сдвинуть вид.");

    ImGui::Separator();

    // --- 2. Показать рамкой ---
    if (UI::Widgets::SvgIconBtn(*ctx.icons, "panel_zoom", "search", 20, toolW, toolH, "Показать рамкой")) {}
    if (ctx.tooltip) ctx.tooltip->Draw("panel.zoom", "Показать рамкой",
        "Зуммирование выделенной области",
        "Основное действие — приблизить область, выделенную рамкой. "
        "Стрелка вниз открывает дополнительные режимы зуммирования.");

    if (UI::Widgets::SvgIconBtn(*ctx.icons, "zoom_dd", "chevron-down", 14, toolW, arrowH, "Доп. режимы зума")) {
        ImGui::OpenPopup("##zoom_menu");
        ImVec2 btnMin = ImGui::GetItemRectMin();
        ImVec2 pp(btnMin.x - 280.0f, btnMin.y);
        if (pp.x < 8.0f) pp.x = 8.0f;
        ImGui::SetNextWindowPos(pp, ImGuiCond_Always);
    }
    if (ImGui::BeginPopup("##zoom_menu")) {
        if (ImGui::MenuItem("Зуммировать")) {}
        if (ImGui::MenuItem("Показать всё")) {
            if (ctx.onShowAll) ctx.onShowAll();
        }
        if (ImGui::MenuItem("Показать рамкой")) {}
        if (ImGui::MenuItem("Показать выбранное")) {}
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // --- 3. Свободная орбита ---
    if (UI::Widgets::SvgIconBtn(*ctx.icons, "panel_orbit", "rotate-cw", 20, toolW, toolH, "Орбита")) {}
    if (ctx.tooltip) ctx.tooltip->Draw("panel.orbit", "Свободная орбита",
        "Вращение модели вокруг центра",
        "Основное действие — свободное вращение вида. Стрелка вниз открывает "
        "различные режимы вращения (с зависимостями, поворот под углом).");

    if (UI::Widgets::SvgIconBtn(*ctx.icons, "orbit_dd", "chevron-down", 14, toolW, arrowH, "Доп. режимы орбиты")) {
        ImGui::OpenPopup("##orbit_menu");
        ImVec2 btnMin = ImGui::GetItemRectMin();
        ImVec2 pp(btnMin.x - 280.0f, btnMin.y);
        if (pp.x < 8.0f) pp.x = 8.0f;
        ImGui::SetNextWindowPos(pp, ImGuiCond_Always);
    }
    if (ImGui::BeginPopup("##orbit_menu")) {
        if (ImGui::MenuItem("Орбита")) {}
        if (ImGui::MenuItem("Орбита с зависимостями")) {}
        if (ImGui::MenuItem("Повернуть под углом")) {}
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // --- 4. Вид на объект ---
    if (UI::Widgets::SvgIconBtn(*ctx.icons, "panel_viewon", "eye", 20, toolW, toolH, "Вид на объект")) {}
    if (ctx.tooltip) ctx.tooltip->Draw("panel.viewon", "Вид на объект",
        "Ориентировать вид по выбранной грани",
        "После активации кликните по любой грани модели — камера развернётся "
        "так, чтобы вы смотрели на эту грань перпендикулярно.");

    ImGui::Separator();

    // --- 5. Стили отображения ---
    if (UI::Widgets::SvgIconBtn(*ctx.icons, "panel_style", "palette", 20, toolW, toolH, "Стиль отображения")) {}
    if (ctx.tooltip) ctx.tooltip->Draw("panel.style", "Стили отображения",
        "Тонированный, каркасный и другие режимы",
        "Основное действие — переключение стиля отображения модели. "
        "Стрелка вниз открывает полный список стилей.");

    if (UI::Widgets::SvgIconBtn(*ctx.icons, "style_dd", "chevron-down", 14, toolW, arrowH, "Все стили отображения")) {
        ImGui::OpenPopup("##style_menu");
        ImVec2 btnMin = ImGui::GetItemRectMin();
        ImVec2 pp(btnMin.x - 280.0f, btnMin.y);
        if (pp.x < 8.0f) pp.x = 8.0f;
        ImGui::SetNextWindowPos(pp, ImGuiCond_Always);
    }
    if (ImGui::BeginPopup("##style_menu")) {
        if (ImGui::MenuItem("Тонированный с рёбрами")) {
            if (ctx.view) ctx.view->SetDisplayModeAll(1, true);
        }
        if (ImGui::MenuItem("Тонированный")) {
            if (ctx.view) ctx.view->SetDisplayModeAll(1, false);
        }
        if (ImGui::MenuItem("Каркасный")) {
            if (ctx.view) ctx.view->SetDisplayModeAll(0, false);
        }
        ImGui::Separator();
        ImGui::MenuItem("Реалистичный (скоро)", nullptr, false, false);
        ImGui::MenuItem("Скрытые линии (скоро)", nullptr, false, false);
        ImGui::MenuItem("Прозрачный (скоро)", nullptr, false, false);
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::End();
}

} // namespace BreezeCAD::UI::Panels
