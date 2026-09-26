#include "ui/ribbon/tabs/ModelTab.h"

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

void DrawModelTab(IconManager& icons, RibbonState& state) {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // === Группа: Эскиз ===
    GroupStart(state);
    SingleButton(icons, "newsketch", "tool.newSketch", ICON_SKETCH, true, true);
    GroupEnd(Tr("grp.sketch").c_str(), state);

    // === Группа: Создать ===
    GroupStart(state);
    SingleButton(icons, "extrude", "tool.extrude", ICON_EXTRUDE, true, true);
    ImGui::SameLine();
    SingleButton(icons, "revolve", "tool.revolve", ICON_REVOLVE, true, true);
    ImGui::SameLine();
    SingleButton(icons, "sweep",   "tool.sweep",   ICON_SWEEP,   true, true);
    ImGui::SameLine();
    SingleButton(icons, "loft",    "tool.loft",    ICON_LOFT,    true, true);
    ImGui::SameLine();
    SingleButton(icons, "spring",  "tool.spring",  "cad/spring", true, true);
    GroupEnd(Tr("grp.create").c_str(), state);

    // === Группа: Изменить ===
    GroupStart(state);
    SingleButton(icons, "hole",     "tool.hole",    ICON_HOLE,    true, true);
    ImGui::SameLine();
    SingleButton(icons, "fillet",   "tool.fillet",  ICON_FILLET,  true, true);
    ImGui::SameLine();
    SingleButton(icons, "thread",   "tool.thread",  ICON_THREAD,  true, true);
    ImGui::SameLine();
    // 2x2 малых СПРАВА от больших
    ImGui::BeginGroup();
        SingleButton(icons, "chamfer",  "tool.chamfer", ICON_CHAMFER, true, false);
        ImGui::SameLine();
        SingleButton(icons, "shell",    "tool.shell",   ICON_SHELL,   true, false);
        SingleButton(icons, "split",    "tool.split",   "scissors",   true, false);
        ImGui::SameLine();
        SingleButton(icons, "rib",      "tool.rib",     "plus",       true, false);
    ImGui::EndGroup();
    GroupEnd(Tr("grp.modify").c_str(), state);

    // === Группа: Массив ===
    GroupStart(state);
    SingleButton(icons, "pattern_rect", "tool.pattern_rect", ICON_PATTERN_RECT, true, false);
    ImGui::SameLine();
    SingleButton(icons, "pattern_circ", "tool.pattern_circ", ICON_PATTERN_CIRC, true, false);
    SingleButton(icons, "mirror3d",     "tool.mirror",       ICON_MIRROR,       true, false);
    GroupEnd(Tr("grp.pattern").c_str(), state);

    // === Группа: Рабочие элементы ===
    GroupStart(state);
    SingleButton(icons, "wplane", "tool.plane",    ICON_PLANE,    true, false);
    ImGui::SameLine();
    SingleButton(icons, "waxis",  "tool.axis",     ICON_AXIS,     true, false);
    SingleButton(icons, "wpoint", "tool.point",    ICON_POINT_3D, true, false);
    GroupEnd(Tr("grp.work").c_str(), state);

    // === Группа: Стандартные изделия ===
    GroupStart(state);
    SingleButton(icons, "library", "tool.library", "package", true, true);
    GroupEnd(Tr("grp.standards").c_str(), state);

    // === Группа: Диагностика ===
    GroupStart(state);
    SingleButton(icons, "info",       "tool.info",       ICON_INFO,    true, false);
    ImGui::SameLine();
    SingleButton(icons, "measure",    "tool.measure",    ICON_MEASURE, true, false);
    SingleButton(icons, "mass",       "tool.mass",       ICON_MASS,    true, false);
    ImGui::SameLine();
    SingleButton(icons, "import_stl", "tool.import_stl", ICON_IMPORT,  true, false);
    GroupEnd(Tr("grp.diagnostics").c_str(), state);
}

}}}
