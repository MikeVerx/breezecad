#include "ui/ribbon/tabs/ViewTab.h"

#include "app/IconManager.h"
#include "app/IconsFontAwesome.h"
#include "app/Localization.h"
#include "ui/ribbon/RibbonButton.h"
#include "ui/ribbon/RibbonGroup.h"
#include "ui/ribbon/RibbonState.h"

#include "imgui.h"

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

void DrawViewTab(IconManager& icons, RibbonState& state) {
    static int dummy = 0;

    // === Навигация (3 кнопки-меню) ===
    GroupStart(state);
    {
        static const char* panOpts[]  = { "tool.pan", "tool.pan", "tool.pan" };
        Button(icons, "nav_pan",   panOpts,  1, dummy, ICON_PAN,   true, true, "Панорамирование");
    }
    ImGui::SameLine();
    {
        static const char* zoomOpts[] = { "tool.zoom_all", "tool.zoom_frame", "tool.zoom_sel", "tool.zoom" };
        int zoomIdx = 0;
        Button(icons, "nav_zoom",  zoomOpts, 4, zoomIdx, ICON_ZOOM,  true, true, "Зум");
    }
    ImGui::SameLine();
    {
        static const char* orbOpts[] = { "tool.orbit", "tool.orbit_dep", "tool.orbit_angle" };
        int orbIdx = 0;
        Button(icons, "nav_orbit", orbOpts, 3, orbIdx, ICON_ORBIT, true, true, "Орбита");
    }
    GroupEnd(Tr("grp.navigation").c_str(), state);

    // === Стиль отображения (7, кнопка-меню) ===
    GroupStart(state);
    {
        static const char* styleOpts[] = {
            "tool.style_realistic", "tool.style_shaded", "tool.style_shaded_edges",
            "tool.style_shaded_hidden", "tool.style_wireframe",
            "tool.style_mono", "tool.style_tech"
        };
        Button(icons, "view_style", styleOpts, 7, state.displayStyleIndex, ICON_SHADOWS, true, true,
               "Стиль отображения");
    }
    ImGui::SameLine();
    {
        static const char* bgOpts[] = {
            "tool.bg_white", "tool.bg_gray", "tool.bg_grad_light",
            "tool.bg_grad_dark", "tool.bg_custom"
        };
        Button(icons, "view_bg", bgOpts, 5, state.backgroundIndex, ICON_BACKGROUND, true, true,
               "Фон вьюпорта");
    }
    GroupEnd(Tr("grp.style").c_str(), state);

    // === Представление модели (6 малых) ===
    GroupStart(state);
    SingleButton(icons, "mdl_shadows",     "tool.shadows",       ICON_SHADOWS,     true, false, "Тени");
    ImGui::SameLine();
    SingleButton(icons, "mdl_reflections", "tool.reflections",   ICON_REFLECTIONS, true, false, "Отражения");
    ImGui::SameLine();
    SingleButton(icons, "mdl_textures",    "tool.textures",      ICON_GRID,        true, false, "Текстуры");
    SingleButton(icons, "mdl_zero_plane",  "tool.zero_plane",    "plus",           true, false, "Нулевая");
    ImGui::SameLine();
    SingleButton(icons, "mdl_refine",      "tool.refine",        ICON_SHOW_ALL,    true, false, "Уточнить");
    GroupEnd(Tr("grp.model_view").c_str(), state);

    // === Видимость (4) ===
    GroupStart(state);
    SingleButton(icons, "hide",        "tool.hide",        ICON_HIDE,        true, false, "Скрыть");
    ImGui::SameLine();
    SingleButton(icons, "show_all",    "tool.show_all",    ICON_SHOW_ALL,    true, false, "Показать всё");
    SingleButton(icons, "isolate",     "tool.isolate",     ICON_ISOLATE,     true, false, "Изолировать");
    ImGui::SameLine();
    SingleButton(icons, "show_hidden", "tool.show_hidden", ICON_SHOW_HIDDEN, true, false, "Показать скрытое");
    GroupEnd(Tr("grp.visibility").c_str(), state);

    // === Окна (5 малых) ===
    GroupStart(state);
    SingleButton(icons, "win_ui",     "tool.ui",           "layout-grid", true, false, "Интерфейс");
    ImGui::SameLine();
    SingleButton(icons, "win_clear",  "tool.clear_screen", ICON_BACKGROUND,  true, false, "Очистить экран");
    ImGui::SameLine();
    SingleButton(icons, "win_switch", "tool.new_window",   ICON_NEW_WINDOW,  true, false, "Переключение");
    SingleButton(icons, "win_new",    "tool.new_window",   "plus",           true, false, "Создать");
    ImGui::SameLine();
    SingleButton(icons, "win_tile",   "tool.tile",         ICON_TILE,        true, false, "Упорядочить");
    GroupEnd(Tr("grp.windows").c_str(), state);
}

}}}
