#include "ThemePresets.h"
#include "imgui.h"

namespace BreezeCAD::UI::Theme {

void SetupLightTheme() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 5.0f;
    s.FrameRounding     = 5.0f;
    s.PopupRounding     = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 5.0f;
    s.TabRounding       = 5.0f;

    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10, 6);
    s.CellPadding       = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 6);
    s.IndentSpacing     = 18.0f;

    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 0.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;

    // ==== Нейтральная серая палитра (Inventor-стиль) ====
    ImVec4 BG_WINDOW(0.941f, 0.941f, 0.941f, 1.00f);   // #F0F0F0 — нейтральный
    ImVec4 BG_PANEL (0.910f, 0.910f, 0.910f, 1.00f);   // #E8E8E8 — панели
    ImVec4 BTN      (0.878f, 0.878f, 0.878f, 1.00f);   // #E0E0E0 — кнопки
    ImVec4 BTN_HOV  (0.831f, 0.831f, 0.831f, 1.00f);   // #D4D4D4
    ImVec4 BTN_ACT  (0.784f, 0.784f, 0.784f, 1.00f);   // #C8C8C8
    ImVec4 BORDER   (0.600f, 0.600f, 0.600f, 1.00f);   // #999999 — темнее
    ImVec4 TXT      (0.000f, 0.000f, 0.000f, 1.00f);   // #000000 — чёрный
    ImVec4 TXT_DIS  (0.350f, 0.350f, 0.350f, 1.00f);   // #595959 — приглушённый

    ImVec4 accent      (0.000f, 0.851f, 0.753f, 1.00f);   // #00D9C0 — бирюза, как в тёмной
    ImVec4 accentHover (0.114f, 0.914f, 0.816f, 1.00f);   // #1DE9D0
    ImVec4 accentActive(0.000f, 0.690f, 0.610f, 1.00f);   // #00B09B

    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]             = BG_WINDOW;
    c[ImGuiCol_ChildBg]              = ImVec4(BG_WINDOW.x, BG_WINDOW.y, BG_WINDOW.z, 0.0f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.988f, 0.992f, 0.996f, 0.98f);
    c[ImGuiCol_MenuBarBg]            = BG_PANEL;

    c[ImGuiCol_Text]                 = TXT;
    c[ImGuiCol_TextDisabled]         = TXT_DIS;

    c[ImGuiCol_Border]               = BORDER;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Button]               = BTN;
    c[ImGuiCol_ButtonHovered]        = BTN_HOV;
    c[ImGuiCol_ButtonActive]         = BTN_ACT;

    c[ImGuiCol_Header]               = BTN;
    c[ImGuiCol_HeaderHovered]        = BTN_HOV;
    c[ImGuiCol_HeaderActive]         = BTN_ACT;

    c[ImGuiCol_Tab]                  = BTN;
    c[ImGuiCol_TabHovered]           = BTN_HOV;
    c[ImGuiCol_TabSelected]          = accent;
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = BG_PANEL;
    c[ImGuiCol_TabDimmedSelected]    = BTN_ACT;

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;

    c[ImGuiCol_TitleBg]              = BG_PANEL;
    c[ImGuiCol_TitleBgActive]        = BG_PANEL;
    c[ImGuiCol_TitleBgCollapsed]     = BG_PANEL;

    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.94f, 0.95f, 0.96f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = BTN_HOV;
    c[ImGuiCol_ScrollbarGrabHovered] = BTN_ACT;
    c[ImGuiCol_ScrollbarGrabActive]  = accent;

    c[ImGuiCol_Separator]            = BORDER;
    c[ImGuiCol_SeparatorHovered]     = accentHover;
    c[ImGuiCol_SeparatorActive]      = accent;

    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0.04f);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.15f, 0.17f, 0.20f, 0.40f);

    c[ImGuiCol_TableHeaderBg]        = BG_PANEL;
    c[ImGuiCol_TableBorderStrong]    = BORDER;
    c[ImGuiCol_TableBorderLight]     = BTN;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0, 0, 0, 0.03f);

    c[ImGuiCol_TextSelectedBg]       = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_NavHighlight]         = accent;
    c[ImGuiCol_PlotLines]            = accent;
    c[ImGuiCol_PlotLinesHovered]     = accentHover;
    c[ImGuiCol_PlotHistogram]        = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
}

void SetupDeepSTheme() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 8.0f;
    s.ChildRounding     = 6.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;

    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10, 6);
    s.CellPadding       = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 6);
    s.IndentSpacing     = 18.0f;

    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 0.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;

    // ==== DeepSColor — глубокий океан с бирюзой ====
    ImVec4 BG_WINDOW(0.059f, 0.078f, 0.098f, 1.00f);   // #0F1419
    ImVec4 BG_PANEL (0.086f, 0.110f, 0.141f, 1.00f);   // #161C24
    ImVec4 BTN      (0.137f, 0.169f, 0.212f, 1.00f);   // #232B36
    ImVec4 BTN_HOV  (0.176f, 0.212f, 0.267f, 1.00f);   // #2D3644
    ImVec4 BTN_ACT  (0.227f, 0.275f, 0.329f, 1.00f);   // #3A4654
    ImVec4 BORDER   (0.165f, 0.204f, 0.255f, 1.00f);   // #2A3441
    ImVec4 TXT      (0.914f, 0.929f, 0.949f, 1.00f);   // #E9EDF2
    ImVec4 TXT_DIS  (0.478f, 0.518f, 0.580f, 1.00f);   // #7A8494

    ImVec4 accent      (0.000f, 0.851f, 0.753f, 1.00f);   // #00D9C0
    ImVec4 accentHover (0.114f, 0.914f, 0.816f, 1.00f);   // #1DE9D0
    ImVec4 accentActive(0.000f, 0.690f, 0.610f, 1.00f);   // #00B09B

    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]             = BG_WINDOW;
    c[ImGuiCol_ChildBg]              = ImVec4(BG_WINDOW.x, BG_WINDOW.y, BG_WINDOW.z, 0.0f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.110f, 0.137f, 0.176f, 0.98f);
    c[ImGuiCol_MenuBarBg]            = BG_PANEL;

    c[ImGuiCol_Text]                 = TXT;
    c[ImGuiCol_TextDisabled]         = TXT_DIS;

    c[ImGuiCol_Border]               = BORDER;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Button]               = BTN;
    c[ImGuiCol_ButtonHovered]        = BTN_HOV;
    c[ImGuiCol_ButtonActive]         = BTN_ACT;

    c[ImGuiCol_Header]               = BTN;
    c[ImGuiCol_HeaderHovered]        = BTN_HOV;
    c[ImGuiCol_HeaderActive]         = BTN_ACT;

    c[ImGuiCol_Tab]                  = BTN;
    c[ImGuiCol_TabHovered]           = BTN_HOV;
    c[ImGuiCol_TabSelected]          = accent;
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = BG_PANEL;
    c[ImGuiCol_TabDimmedSelected]    = BTN_ACT;

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;

    c[ImGuiCol_TitleBg]              = BG_PANEL;
    c[ImGuiCol_TitleBgActive]        = BG_PANEL;
    c[ImGuiCol_TitleBgCollapsed]     = BG_PANEL;

    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.08f, 0.10f, 0.13f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = BTN_HOV;
    c[ImGuiCol_ScrollbarGrabHovered] = BTN_ACT;
    c[ImGuiCol_ScrollbarGrabActive]  = accent;

    c[ImGuiCol_Separator]            = BORDER;
    c[ImGuiCol_SeparatorHovered]     = accentHover;
    c[ImGuiCol_SeparatorActive]      = accent;

    c[ImGuiCol_ResizeGrip]           = ImVec4(1, 1, 1, 0.04f);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.05f, 0.06f, 0.08f, 0.60f);

    c[ImGuiCol_TableHeaderBg]        = BTN;
    c[ImGuiCol_TableBorderStrong]    = BORDER;
    c[ImGuiCol_TableBorderLight]     = BTN;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.02f);

    c[ImGuiCol_TextSelectedBg]       = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_NavHighlight]         = accent;
    c[ImGuiCol_PlotLines]            = accent;
    c[ImGuiCol_PlotLinesHovered]     = accentHover;
    c[ImGuiCol_PlotHistogram]        = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
}


} // namespace BreezeCAD::UI::Theme
