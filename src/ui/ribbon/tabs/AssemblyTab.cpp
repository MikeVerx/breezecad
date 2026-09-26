#include "ui/ribbon/tabs/AssemblyTab.h"

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

void DrawAssemblyTab(IconManager& icons, RibbonState& state) {
    // === Компонент ===
    GroupStart(state);
    SingleButton(icons, "asm_place",   "tool.asm.place",   "package-plus", true, true, "Вставить компонент");
    ImGui::SameLine();
    SingleButton(icons, "asm_create",  "tool.asm.create",  "package",      true, true, "Создать компонент");
    ImGui::SameLine();
    SingleButton(icons, "asm_replace", "tool.asm.replace", "refresh-cw",   true, true, "Заменить компонент");
    ImGui::SameLine();
    SingleButton(icons, "asm_copy",    "tool.asm.copy",    ICON_COPY,      true, true, "Копировать компонент");
    GroupEnd(Tr("grp.component").c_str(), state);

    // === Позиционирование ===
    GroupStart(state);
    SingleButton(icons, "asm_joint",      "tool.asm.joint",      "link",    true, true, "Соединение (подвижное)");
    ImGui::SameLine();
    SingleButton(icons, "asm_constraint", "tool.asm.constraint", "pin",     true, true, "Зависимость (статичная)");
    ImGui::SameLine();
    SingleButton(icons, "asm_dof",        "tool.asm.dof",        "move-3d", true, true, "Степени свободы");
    GroupEnd(Tr("grp.position").c_str(), state);

    // === Массив ===
    GroupStart(state);
    SingleButton(icons, "asm_pat_rect", "tool.pattern_rect", ICON_PATTERN_RECT, true, false, "Прямоуг.");
    ImGui::SameLine();
    SingleButton(icons, "asm_pat_circ", "tool.pattern_circ", ICON_PATTERN_CIRC, true, false, "Круговой");
    SingleButton(icons, "asm_mirror",   "tool.mirror",       ICON_MIRROR,       true, false, "Зеркало");
    GroupEnd(Tr("grp.pattern").c_str(), state);

    // === Спецификация ===
    GroupStart(state);
    SingleButton(icons, "asm_bom",  "tool.bom",           "list",    true, true, "Bill of Materials");
    ImGui::SameLine();
    SingleButton(icons, "asm_spec", "tool.specification", ICON_FILE, true, true, "Спецификация");
    GroupEnd(Tr("grp.specification").c_str(), state);

    // === Стандартные изделия ===
    GroupStart(state);
    SingleButton(icons, "asm_library", "tool.library", "package", true, true, "Библиотека");
    GroupEnd(Tr("grp.standards").c_str(), state);
}

}}}
