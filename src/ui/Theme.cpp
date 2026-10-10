#include "ui/Theme.h"

#include <imgui.h>

namespace bz::ui {

Theme Theme::Light() {
    Theme t;
    t.dark = false;
    ThemeColors& c = t.colors;
    c.appBackground   = Color::Hex(0xF4F5F7);
    c.ribbon          = Color::Hex(0xFBFBFC);
    c.panel           = Color::Hex(0xFFFFFF);
    c.statusBar       = Color::Hex(0xECEEF1);
    c.field           = Color::Hex(0xFFFFFF);
    c.fieldBorder     = Color::Hex(0xDADDE2);
    c.segmentTrack    = Color::Hex(0xF1F3F6);
    c.segmentActive   = Color::Hex(0xFFFFFF);
    c.divider         = Color::Hex(0xE1E4E8);
    c.dividerSoft     = Color::Hex(0xEEF0F2);

    c.hover           = Color::Hex(0xE6E9EE);
    c.hoverRow        = Color::Hex(0xF1F4F8);
    c.selectedRow     = Color::Hex(0xE8F0FC);
    c.pressed         = Color::Hex(0xDCE1E8);

    c.text            = Color::Hex(0x1B1F24);
    c.textSecondary   = Color::Hex(0x4A525E);
    c.textMuted       = Color::Hex(0x5A626E);
    c.textDisabled    = Color::Hex(0xA0A7B1);
    c.textOnAccent    = Color::Hex(0xFFFFFF);
    c.tooltip         = Color::Hex(0x1B1F24);
    c.tooltipText     = Color::Hex(0xF4F5F7);

    c.accent          = Color::Hex(0x1D4ED8);
    c.accentHover     = Color::Hex(0x1A3FB0);
    c.accentSoft      = Color::Hex(0xE6EEFB);
    c.accentSoftBorder= Color::Hex(0xBFD3F5);

    c.success         = Color::Hex(0x2E7D4F);
    c.warning         = Color::Hex(0xB45309);
    c.danger          = Color::Hex(0xE5484D);

    c.viewportTop     = Color::Hex(0xFAFBFC);
    c.viewportBottom  = Color::Hex(0xE4E8ED);
    c.body            = Color::Hex(0xC9CFD7);
    c.bodyEdges       = Color::Hex(0x3D4552);
    c.hoverHighlight  = Color::Hex(0x38A9E8);
    c.selection       = Color::Hex(0x1D4ED8);

    c.sketchFree      = Color::Hex(0x1D4ED8);
    c.sketchDefined   = Color::Hex(0x1B1F24);
    c.sketchConstruction = Color::Hex(0x6F7884);
    c.sketchProjected    = Color::Hex(0xC98A0B);
    c.sketchHover     = Color::Hex(0x38A9E8);
    c.sketchSelected  = Color::Hex(0xEA7A1E);
    c.sketchPoint     = Color::Hex(0x1680C4);
    c.sketchGrid      = Color::Hex(0x1B1F24, 0.05f);
    c.sketchGridMajor = Color::Hex(0x1B1F24, 0.10f);
    c.dimension       = Color::Hex(0x0A4F7E);
    c.constraintGlyph = Color::Hex(0xB45309);
    c.regionFill      = Color::Hex(0x38A9E8, 0.28f);
    c.planeFill       = Color::Hex(0xE9B44C, 0.18f);
    c.planeEdge       = Color::Hex(0xC28A23, 0.85f);
    c.axisX           = Color::Hex(0xD64545);
    c.axisY           = Color::Hex(0x2E9E5B);
    c.axisZ           = Color::Hex(0x1D4ED8);
    c.planeXY         = Color::Hex(0x7088B0);
    c.planeXZ         = Color::Hex(0x739A84);
    c.planeYZ         = Color::Hex(0xA9807E);

    c.cubeLight       = Color::Hex(0xFFFFFF);
    c.cubeShade       = Color::Hex(0xC4CBD4);
    c.cubeHover       = Color::Hex(0xAAC7FC);
    c.cubeStroke      = Color::Hex(0x9DA6B2);
    c.cubeInner       = Color::Hex(0x3C4655, 0.14f);
    c.cubeText        = Color::Hex(0x3A424E);
    c.floatingButton  = Color::Hex(0xFFFFFF, 0.88f);
    c.floatingBorder  = Color::Hex(0xD5DAE0);
    c.floatingInk     = Color::Hex(0x3D4552);
    return t;
}

Theme Theme::Dark() {
    Theme t = Light();
    t.dark = true;
    ThemeColors& c = t.colors;
    c.appBackground   = Color::Hex(0x16181C);
    c.ribbon          = Color::Hex(0x1D2025);
    c.panel           = Color::Hex(0x1B1E22);
    c.statusBar       = Color::Hex(0x121417);
    c.field           = Color::Hex(0x23272D);
    c.fieldBorder     = Color::Hex(0x353A42);
    c.segmentTrack    = Color::Hex(0x15171B);
    c.segmentActive   = Color::Hex(0x2E333B);
    c.divider         = Color::Hex(0x262A30);
    c.dividerSoft     = Color::Hex(0x23272D);

    c.hover           = Color::Hex(0x2A2E36);
    c.hoverRow        = Color::Hex(0x22262C);
    c.selectedRow     = Color::Hex(0x1F2A40);
    c.pressed         = Color::Hex(0x30353D);

    c.text            = Color::Hex(0xE6E8EB);
    c.textSecondary   = Color::Hex(0xAEB4BD);
    c.textMuted       = Color::Hex(0x9AA1AB);
    c.textDisabled    = Color::Hex(0x5C636E);
    c.textOnAccent    = Color::Hex(0xFFFFFF);
    c.tooltip         = c.field;
    c.tooltipText     = c.text;

    c.accent          = Color::Hex(0x3F6FEA);
    c.accentHover     = Color::Hex(0x4F7DF5);
    c.accentSoft      = Color::Hex(0x1F2A40);
    c.accentSoftBorder= Color::Hex(0x3A5BA0);

    c.success         = Color::Hex(0x4CBB7F);
    c.warning         = Color::Hex(0xF5A524);
    c.danger          = Color::Hex(0xF06A6E);

    c.viewportTop     = Color::Hex(0x2A2E35);
    c.viewportBottom  = Color::Hex(0x17191D);
    c.body            = Color::Hex(0xA7AFBA);
    c.bodyEdges       = Color::Hex(0x0F1114);
    c.hoverHighlight  = Color::Hex(0x5BB6EE);
    c.selection       = Color::Hex(0x6E9BFF);

    c.sketchFree      = Color::Hex(0x6E9BFF);
    c.sketchDefined   = Color::Hex(0xE6E8EB);
    c.sketchConstruction = Color::Hex(0x8E95A0);
    c.sketchProjected    = Color::Hex(0xF0B43C);
    c.sketchHover     = Color::Hex(0x5BB6EE);
    c.sketchSelected  = Color::Hex(0xF59E4B);
    c.sketchPoint     = Color::Hex(0x5BB6EE);
    c.sketchGrid      = Color::Hex(0xFFFFFF, 0.05f);
    c.sketchGridMajor = Color::Hex(0xFFFFFF, 0.10f);
    c.dimension       = Color::Hex(0x8DABFF);
    c.constraintGlyph = Color::Hex(0xF5A524);
    c.regionFill      = Color::Hex(0x5BB6EE, 0.30f);
    c.planeFill       = Color::Hex(0xE9B44C, 0.16f);
    c.planeEdge       = Color::Hex(0xE9B44C, 0.85f);
    c.axisX           = Color::Hex(0xF06A6E);
    c.axisY           = Color::Hex(0x4CBB7F);
    c.axisZ           = Color::Hex(0x6E9BFF);
    c.planeXY         = Color::Hex(0x8C9CC0);
    c.planeXZ         = Color::Hex(0x8AAE98);
    c.planeYZ         = Color::Hex(0xBE9592);

    c.cubeLight       = Color::Hex(0x5C6470);
    c.cubeShade       = Color::Hex(0x2E333B);
    c.cubeHover       = Color::Hex(0x3D62C4);
    c.cubeStroke      = Color::Hex(0x6A727E);
    c.cubeInner       = Color::Hex(0xFFFFFF, 0.10f);
    c.cubeText        = Color::Hex(0xDDE1E6);
    c.floatingButton  = Color::Hex(0x262A31, 0.90f);
    c.floatingBorder  = Color::Hex(0x3A404A);
    c.floatingInk     = Color::Hex(0xC4C9D1);
    return t;
}

namespace {

ImVec4 V(const Color& c) { return {c.r, c.g, c.b, c.a}; }

} // namespace

void ApplyToImGui(const Theme& theme, float scale) {
    ImGuiStyle style;
    const ThemeColors& c = theme.colors;

    style.WindowPadding     = {0, 0};
    style.WindowRounding    = 0;
    style.WindowBorderSize  = 0;
    style.ChildRounding     = 0;
    style.PopupRounding     = theme.metrics.radius;
    style.PopupBorderSize   = 1;
    style.FramePadding      = {10, 7};
    style.FrameRounding     = theme.metrics.radiusSmall;
    style.FrameBorderSize   = 1;
    style.ItemSpacing       = {8, 6};
    style.ItemInnerSpacing  = {6, 4};
    style.ScrollbarSize     = 10;
    style.ScrollbarRounding = 5;
    style.GrabRounding      = 4;
    style.TabRounding       = theme.metrics.radiusSmall;
    style.SeparatorTextBorderSize = 1;
    style.SelectableTextAlign = {0, 0.5f};
    style.AntiAliasedLines  = true;
    style.AntiAliasedFill   = true;

    ImVec4* col = style.Colors;
    col[ImGuiCol_Text]                 = V(c.text);
    col[ImGuiCol_TextDisabled]         = V(c.textDisabled);
    col[ImGuiCol_WindowBg]             = V(c.panel);
    col[ImGuiCol_ChildBg]              = {0, 0, 0, 0};
    col[ImGuiCol_PopupBg]              = V(c.panel);
    col[ImGuiCol_Border]               = V(c.divider);
    col[ImGuiCol_BorderShadow]         = {0, 0, 0, 0};
    col[ImGuiCol_FrameBg]              = V(c.field);
    col[ImGuiCol_FrameBgHovered]       = V(c.field);
    col[ImGuiCol_FrameBgActive]        = V(c.field);
    col[ImGuiCol_TitleBg]              = V(c.appBackground);
    col[ImGuiCol_TitleBgActive]        = V(c.appBackground);
    col[ImGuiCol_MenuBarBg]            = V(c.appBackground);
    col[ImGuiCol_ScrollbarBg]          = {0, 0, 0, 0};
    col[ImGuiCol_ScrollbarGrab]        = V(c.fieldBorder);
    col[ImGuiCol_ScrollbarGrabHovered] = V(c.textDisabled);
    col[ImGuiCol_ScrollbarGrabActive]  = V(c.textMuted);
    col[ImGuiCol_CheckMark]            = V(c.accent);
    col[ImGuiCol_SliderGrab]           = V(c.accent);
    col[ImGuiCol_SliderGrabActive]     = V(c.accentHover);
    col[ImGuiCol_Button]               = {0, 0, 0, 0};
    col[ImGuiCol_ButtonHovered]        = V(c.hover);
    col[ImGuiCol_ButtonActive]         = V(c.pressed);
    col[ImGuiCol_Header]               = V(c.selectedRow);
    col[ImGuiCol_HeaderHovered]        = V(c.hoverRow);
    col[ImGuiCol_HeaderActive]         = V(c.selectedRow);
    col[ImGuiCol_Separator]            = V(c.divider);
    col[ImGuiCol_SeparatorHovered]     = V(c.accent);
    col[ImGuiCol_SeparatorActive]      = V(c.accent);
    col[ImGuiCol_TextSelectedBg]       = V(c.accent.WithAlpha(0.25f));
    col[ImGuiCol_NavCursor]            = V(c.accent);
    col[ImGuiCol_ModalWindowDimBg]     = V(Color::Hex(0x0B0D10, 0.35f));
    col[ImGuiCol_TableHeaderBg]        = V(c.panel);
    col[ImGuiCol_TableBorderLight]     = V(c.dividerSoft);
    col[ImGuiCol_TableBorderStrong]    = V(c.divider);

    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
}

} // namespace bz::ui
