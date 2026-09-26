#pragma once
// ============================================================
// ThemeTokens — цветовые токены интерфейса.
//
// Заполняются в UI::Theme::ApplyTheme() (ThemeManager.cpp).
// Читаются везде, где нужен цвет «от темы»: home screen,
// ViewCube, панели, диалоги. Никаких хардкод-констант.
//
// namespace — BreezeCAD (без под-namespace, для краткости
// использования в остальном коде).
// ============================================================

#include "imgui.h"

namespace BreezeCAD {

struct ThemeTokens {
    // ==== Home Screen / общий UI ====
    ImU32 bgTitle;
    ImU32 bgRibbon;
    ImU32 bgContent;
    ImU32 bgCardBg;
    ImU32 border;
    ImU32 borderStrong;
    ImU32 textMain;
    ImU32 textDim;
    ImU32 homeBtnLabel;
    ImU32 homeBtnBg;
    ImU32 homeBtnBgHover;
    ImU32 homeBtnBgDisabled;
    ImU32 homeBtnTextDisabled;
    ImU32 thumbBg;
    ImU32 accent;
    ImU32 accentHover;

    // ==== ViewCube ====
    ImU32 cubeBgCircle;
    ImU32 cubeFaceTop;
    ImU32 cubeFaceBottom;
    ImU32 cubeFaceFront;
    ImU32 cubeFaceBack;
    ImU32 cubeFaceRight;
    ImU32 cubeFaceLeft;
    ImU32 cubeOutline;
    ImU32 cubeText;
    ImU32 cubeHover;
    ImU32 cubeFaceHover;
};

} // namespace BreezeCAD
