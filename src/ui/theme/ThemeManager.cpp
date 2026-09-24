#include "ThemeManager.h"
#include "ThemePresets.h"
#include "ThemeTokens.h"
#include "occt/OcctView.h"

#include "imgui.h"
#include "Quantity_Color.hxx"

namespace BreezeCAD::UI::Theme {

void ApplyTheme(int theme, ThemeTokens& myTheme, OcctView* view) {
    // По ТЗ три темы:
    //   0 = Светлая (нейтральный серо-белый, оранжевый акцент)
    //   1 = Тёмная  (бирюзовый акцент, бывшая DeepSColor)
    //   2 = Авто    (по системе; пока используем Тёмную)
    // Старое значение 3 (DeepSColor) маппится на 1.
    if (theme == 3) theme = 1;


    if (theme == 0) {
        ImGui::StyleColorsLight();
        UI::Theme::SetupLightTheme();

        // Принудительно чёрный текст и иконки (override любых стилей)
        ImGui::GetStyle().Colors[ImGuiCol_Text]         = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

        // ==== Токены светлой темы (Inventor-style: серо-градиентная) ====
        myTheme.bgTitle             = IM_COL32(178, 182, 190, 255);
        myTheme.bgRibbon            = IM_COL32(190, 194, 202, 255);
        myTheme.bgContent           = IM_COL32(214, 218, 222, 255);
        myTheme.bgCardBg            = IM_COL32(202, 206, 212, 255);
        myTheme.border              = IM_COL32(140, 144, 152, 255);
        myTheme.borderStrong        = IM_COL32(112, 116, 124, 255);
        myTheme.textMain            = IM_COL32(  0,   0,   0, 255);
        myTheme.textDim             = IM_COL32( 52,  56,  64, 255);
        myTheme.homeBtnLabel        = IM_COL32(  0,   0,   0, 255);
        myTheme.homeBtnBg           = IM_COL32(198, 200, 204, 255);
        myTheme.homeBtnBgHover      = IM_COL32(178, 182, 188, 255);
        myTheme.homeBtnBgDisabled   = IM_COL32(184, 186, 190, 200);
        myTheme.homeBtnTextDisabled = IM_COL32( 96, 100, 108, 220);
        myTheme.thumbBg             = IM_COL32(198, 202, 208, 255);
        myTheme.accent              = IM_COL32( 40, 120, 216, 255);
        myTheme.accentHover         = IM_COL32( 64, 144, 232, 255);

        myTheme.cubeBgCircle        = IM_COL32(200, 204, 210, 180);
        myTheme.cubeFaceTop         = IM_COL32(232, 232, 232, 255);
        myTheme.cubeFaceBottom      = IM_COL32(184, 184, 184, 255);
        myTheme.cubeFaceFront       = IM_COL32(208, 208, 208, 255);
        myTheme.cubeFaceBack        = IM_COL32(200, 200, 200, 255);
        myTheme.cubeFaceRight       = IM_COL32(216, 216, 216, 255);
        myTheme.cubeFaceLeft        = IM_COL32(192, 192, 192, 255);
        myTheme.cubeOutline         = IM_COL32(  0,   0,   0, 255);
        myTheme.cubeText            = IM_COL32(  0,   0,   0, 255);
        myTheme.cubeHover           = IM_COL32( 40, 120, 216, 200);
        myTheme.cubeFaceHover       = IM_COL32( 40, 120, 216, 120);

        if (view) {
            Quantity_Color top(0.66, 0.72, 0.78, Quantity_TOC_RGB);      // #A8B8C7
            Quantity_Color bottom(0.85, 0.88, 0.91, Quantity_TOC_RGB);   // #D9E0E8
            view->SetBackgroundGradient(top, bottom);
        }
    } else {
        // 1 = Тёмная, 2 = Авто (пока тоже тёмная)
        ImGui::StyleColorsDark();
        UI::Theme::SetupDeepSTheme();

        // ==== Токены тёмной темы ====
        myTheme.bgTitle             = IM_COL32( 28,  32,  40, 255);
        myTheme.bgRibbon            = IM_COL32( 34,  38,  48, 255);
        myTheme.bgContent           = IM_COL32( 22,  24,  30, 255);
        myTheme.bgCardBg            = IM_COL32( 32,  36,  46, 255);
        myTheme.border              = IM_COL32( 60,  68,  82, 255);
        myTheme.borderStrong        = IM_COL32( 60,  70,  90, 255);
        myTheme.textMain            = IM_COL32(220, 230, 245, 255);
        myTheme.textDim             = IM_COL32(120, 130, 150, 255);
        myTheme.homeBtnLabel        = IM_COL32(220, 230, 245, 255);
        myTheme.homeBtnBg           = IM_COL32( 50,  58,  72, 255);
        myTheme.homeBtnBgHover      = IM_COL32( 60,  72,  92, 255);
        myTheme.homeBtnBgDisabled   = IM_COL32( 40,  45,  55, 180);
        myTheme.homeBtnTextDisabled = IM_COL32(120, 130, 150, 220);
        myTheme.thumbBg             = IM_COL32( 24,  28,  36, 255);
        myTheme.accent              = IM_COL32( 51, 140, 242, 255);
        myTheme.accentHover         = IM_COL32( 76, 164, 255, 255);

        myTheme.cubeBgCircle        = IM_COL32( 40,  45,  55, 180);
        myTheme.cubeFaceTop         = IM_COL32(180, 195, 210, 255);
        myTheme.cubeFaceBottom      = IM_COL32(120, 130, 145, 255);
        myTheme.cubeFaceFront       = IM_COL32(160, 175, 195, 255);
        myTheme.cubeFaceBack        = IM_COL32(140, 155, 175, 255);
        myTheme.cubeFaceRight       = IM_COL32(150, 165, 185, 255);
        myTheme.cubeFaceLeft        = IM_COL32(130, 145, 165, 255);
        myTheme.cubeOutline         = IM_COL32( 30,  35,  45, 255);
        myTheme.cubeText            = IM_COL32( 25,  30,  40, 255);
        myTheme.cubeHover           = IM_COL32( 51, 140, 242, 200);
        myTheme.cubeFaceHover       = IM_COL32( 51, 140, 242, 120);

        if (view) {
            Quantity_Color top(0.040, 0.075, 0.130, Quantity_TOC_RGB);
            Quantity_Color bottom(0.010, 0.025, 0.050, Quantity_TOC_RGB);
            view->SetBackgroundGradient(top, bottom);
        }
    }
}


} // namespace BreezeCAD::UI::Theme
