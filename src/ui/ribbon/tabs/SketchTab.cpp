#include "ui/ribbon/tabs/SketchTab.h"

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

void DrawSketchTab(SketchTabContext& ctx) {
    IconManager& icons = *ctx.icons;
    RibbonState& state = *ctx.state;
    bool inSketch = ctx.inSketch;

    static int dummy = 0;

    // Отрезок: Отрезок / Сплайн(упр.верш.) / Сплайн(интерп.) / Кривая по формуле / Замкнуть
    static const char* lineOpts[]   = {
        "tool.line", "tool.spline.ctrl", "tool.spline.interp",
        "tool.curve.fx", "tool.close"
    };
    // Прямоугольник: 4 прямоугольника + 5 пазов + Многоугольник
    static const char* rectOpts[]   = {
        "tool.rect.2pt", "tool.rect.3pt",
        "tool.rect.2pt.center", "tool.rect.3pt.center",
        "tool.slot.2center", "tool.slot.common", "tool.slot.center",
        "tool.slot.arc3pt", "tool.slot.arc.center",
        "tool.polygon"
    };
    static const char* circleOpts[] = { "tool.circle.center", "tool.circle.tan", "tool.ellipse" };
    static const char* arcOpts[]    = { "tool.arc.3pt", "tool.arc.tan", "tool.arc.center" };
    static const char* pointOpt[]   = { "tool.point" };
    static const char* textOpt[]    = { "tool.text", "tool.text.line" };
    static const char* fillet2dOpts[] = { "tool.fillet.2d", "tool.chamfer.2d" };
    static const char* projOpt[] = {
        "tool.proj.geom", "tool.project.loop", "tool.proj.unfold",
        "tool.proj.3d", "tool.proj.dwg"
    };
    static int projIdx = 0;
    static const char* trimOpt[]    = { "tool.trim", "tool.extend" };
    static const char* offsetOpt[]  = { "tool.offset" };
    static const char* moveOpt[]    = { "tool.move", "tool.copy", "tool.rotate" };
    static const char* scaleOpt[]   = { "tool.scale", "tool.stretch" };
    static const char* splitOpt[]   = { "tool.split" };
    static const char* finishOpt[]  = { "tool.finish" };
    static const char* clearOpt[]   = { "tool.clear" };
    static const char* showFormatOpt[] = { "tool.show_format" };

    // ============================================================
    // СОЗДАТЬ — 6 больших в один ряд
    // ============================================================
    GroupStart(state);
    {
        int r = Button(icons, "line", lineOpts, 5, state.lineToolIndex, ICON_LINE, inSketch, true,
                       "Отрезок");
        if (r == 1 || r == 3) {
            if (ctx.currentLineStyle) *ctx.currentLineStyle = LS_Normal;
            if (ctx.hasSketchStart)   *ctx.hasSketchStart   = false;
            if (ctx.clearPreviewLine) ctx.clearPreviewLine();
        }
    }
    ImGui::SameLine();
    Button(icons, "rect",   rectOpts,   10, state.rectToolIndex,   ICON_RECT,    inSketch, true, "Прямоугольник");
    ImGui::SameLine();
    Button(icons, "circle", circleOpts, 3,  state.circleToolIndex, ICON_CIRCLE,  inSketch, true, "Окружность");
    ImGui::SameLine();
    Button(icons, "arc",    arcOpts,    3,  state.arcToolIndex,    ICON_ARC,     inSketch, true, "Дуга");
    ImGui::SameLine();
    Button(icons, "point",  pointOpt,   1,  dummy,             ICON_POINT,   inSketch, true, "Точка");
    ImGui::SameLine();
    Button(icons, "text",   textOpt,    2,  dummy,             ICON_TEXT,    inSketch, true, "Текст");
    ImGui::SameLine();
    Button(icons, "fillet_2d", fillet2dOpts, 2, dummy, ICON_FILLET, inSketch, true, "Сопряжение");
    GroupEnd(Tr("grp.create").c_str(), state);

    // ============================================================
    // ПРОЕЦИРОВАТЬ
    // ============================================================
    GroupStart(state);
    Button(icons, "proj_geom", projOpt, 5, projIdx, ICON_PROJECT, inSketch, true, "Проецирование");
    GroupEnd(Tr("grp.project").c_str(), state);

    // ============================================================
    // МАССИВ — 3 малые
    // ============================================================
    GroupStart(state);
    Button(icons, "pat_rect", pointOpt, 1, dummy, ICON_PATTERN_RECT, inSketch, false, "Прямоугольный массив");
    ImGui::SameLine();
    Button(icons, "pat_circ", pointOpt, 1, dummy, ICON_PATTERN_CIRC, inSketch, false, "Круговой массив");
    Button(icons, "pat_mirr", pointOpt, 1, dummy, ICON_MIRROR,       inSketch, false, "Зеркало");
    GroupEnd(Tr("grp.pattern").c_str(), state);

    // ============================================================
    // ИЗМЕНИТЬ — 5 кнопок-меню
    // ============================================================
    GroupStart(state);
    Button(icons, "move",   moveOpt,   3, state.moveToolIndex,   ICON_MOVE,   inSketch, false, "Перенести");
    ImGui::SameLine();
    Button(icons, "trim",   trimOpt,   2, state.trimToolIndex,   ICON_TRIM,   inSketch, false, "Обрезать");
    ImGui::SameLine();
    Button(icons, "offset", offsetOpt, 1, state.offsetToolIndex, ICON_OFFSET, inSketch, false, "Сместить");
    Button(icons, "scale",  scaleOpt,  2, dummy,             ICON_SCALE,  inSketch, false, "Масштаб");
    ImGui::SameLine();
    Button(icons, "split",  splitOpt,  1, dummy,             ICON_SPLIT,  inSketch, false, "Разделить");
    ImGui::SameLine();
    Button(icons, "copy2d", splitOpt,  1, dummy,             ICON_COPY,   inSketch, false, "Копировать");
    GroupEnd(Tr("grp.modify").c_str(), state);

    // ============================================================
    // ОГРАНИЧЕНИЯ — 12 малых, 2 ряда по 6
    // ============================================================
    GroupStart(state);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    Button(icons, "con_coincident",  pointOpt, 1, dummy, ICON_COINCIDENT,    inSketch, false, "Совпадение");
    ImGui::SameLine();
    Button(icons, "con_parallel",    pointOpt, 1, dummy, ICON_PARALLEL,      inSketch, false, "Параллельность");
    ImGui::SameLine();
    Button(icons, "con_perp",        pointOpt, 1, dummy, ICON_PERPENDICULAR, inSketch, false, "Перпендикулярность");
    ImGui::SameLine();
    Button(icons, "con_horiz",       pointOpt, 1, dummy, ICON_HORIZONTAL,    inSketch, false, "Горизонталь");
    ImGui::SameLine();
    Button(icons, "con_vert",        pointOpt, 1, dummy, ICON_VERTICAL,      inSketch, false, "Вертикаль");
    ImGui::SameLine();
    Button(icons, "con_tangent",     pointOpt, 1, dummy, ICON_TANGENT,       inSketch, false, "Касание");
    Button(icons, "con_concentric",  pointOpt, 1, dummy, ICON_CONCENTRIC,    inSketch, false, "Концентричность");
    ImGui::SameLine();
    Button(icons, "con_equal",       pointOpt, 1, dummy, ICON_EQUAL,         inSketch, false, "Равенство");
    ImGui::SameLine();
    Button(icons, "con_collinear",   pointOpt, 1, dummy, ICON_COLLINEAR,     inSketch, false, "Коллинеарность");
    ImGui::SameLine();
    Button(icons, "con_fix",         pointOpt, 1, dummy, ICON_FIX,           inSketch, false, "Фиксация");
    ImGui::SameLine();
    Button(icons, "con_symmetric",   pointOpt, 1, dummy, ICON_SYMMETRIC,     inSketch, false, "Симметрия");
    ImGui::SameLine();
    Button(icons, "con_smooth",      pointOpt, 1, dummy, ICON_SMOOTH_G2,     inSketch, false, "Сглаживание G2");
    ImGui::PopStyleVar();
    GroupEnd(Tr("grp.constrain").c_str(), state);

    // ============================================================
    // РАЗМЕРЫ
    // ============================================================
    GroupStart(state);
    Button(icons, "dim_linear",   pointOpt, 1, dummy, ICON_DIM_LINEAR,   inSketch, false, "Линейный");
    ImGui::SameLine();
    Button(icons, "dim_angle",    pointOpt, 1, dummy, ICON_DIM_ANGLE,    inSketch, false, "Угловой");
    ImGui::SameLine();
    Button(icons, "dim_radius",   pointOpt, 1, dummy, ICON_DIM_RADIUS,   inSketch, false, "Радиус");
    Button(icons, "dim_diameter", pointOpt, 1, dummy, ICON_DIM_DIAMETER, inSketch, false, "Диаметр");
    ImGui::SameLine();
    Button(icons, "dim_ordinate", pointOpt, 1, dummy, ICON_DIM_ORDINATE, inSketch, false, "Ординатный");
    GroupEnd(Tr("grp.dimensions").c_str(), state);

    // ============================================================
    // ВСТАВКА
    // ============================================================
    GroupStart(state);
    Button(icons, "insert_img",    pointOpt, 1, dummy, ICON_INSERT_IMG, inSketch, false, "Изображение");
    ImGui::SameLine();
    Button(icons, "insert_points", pointOpt, 1, dummy, ICON_POINT_3D,   inSketch, false, "Точки");
    Button(icons, "insert_acad",   pointOpt, 1, dummy, ICON_FILE,       inSketch, false, "ACAD");
    GroupEnd(Tr("grp.insert").c_str(), state);

    // ============================================================
    // ФОРМАТ — 3 тонкие кнопки в столбик
    // ============================================================
    GroupStart(state);
    {
        struct LSItem { const char* label; const char* tip; LineStyle style; ImU32 bg; };
        LSItem items[] = {
            { "Основная",   "Основная линия",        LS_Normal,       IM_COL32(140, 51, 191, 255) },
            { "Вспомог.",   "Вспомогательная линия", LS_Construction, IM_COL32(115, 128, 166, 255) },
            { "Осевая",     "Осевая линия",          LS_Centerline,   IM_COL32(179, 102, 38, 255) },
        };
        LineStyle curStyle = ctx.currentLineStyle ? *ctx.currentLineStyle : LS_Normal;
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            bool active = (curStyle == items[i].style);
            ImVec4 bg = active ? ImColor(items[i].bg).Value
                               : ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(bg.x*1.2f, bg.y*1.2f, bg.z*1.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(bg.x*0.8f, bg.y*0.8f, bg.z*0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            char buf[64];
            snprintf(buf, sizeof(buf), "%s##ls_%d", items[i].label, i);
            if (ImGui::Button(buf, ImVec2(85, 62))) {
                if (ctx.currentLineStyle) *ctx.currentLineStyle = items[i].style;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", items[i].tip);
            ImGui::PopStyleColor(4);
        }
    }
    GroupEnd(Tr("grp.format").c_str(), state);

    // ============================================================
    // ВЫХОД
    // ============================================================
    GroupStart(state);
    if (Button(icons, "finish", finishOpt, 1, dummy, ICON_FINISH, inSketch, true, "Завершить эскиз")) {
        if (ctx.exitSketch) ctx.exitSketch();
    }
    ImGui::SameLine();
    if (Button(icons, "show_format", showFormatOpt, 1, dummy, "layout-grid", inSketch, true, "Показать формат")) {
        // TODO: чекбоксы линий / размеров / ограничений
    }
    GroupEnd(Tr("grp.exit").c_str(), state);
}

}}}
