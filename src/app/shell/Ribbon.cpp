// Mode tabs and the ribbon. Commands that are not built yet are shown
// greyed out with a note, so the layout matches the approved mockup.

#include "app/features/OperationEditors.h"
#include "app/shell/Shell.h"
#include "app/sketch/SketchEditor.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <algorithm>

namespace bz::app {
namespace {

constexpr const char* kLater = N_("Появится в следующих версиях");

/// Lays ribbon groups out left to right.
class RibbonBuilder {
public:
    RibbonBuilder(AppContext& app, ImVec2 origin, float height) : myApp(app), myUi(app.ui), myTop(origin.y), myHeight(height) {
        myX = origin.x + myUi.S(8);
    }

    AppContext& App() const { return myApp; }
    /// Right edge of what was laid out so far.
    float End() const { return myX; }

    void BeginGroup(const char* caption) {
        myCaption = caption;
        myGroupStart = myX + myUi.S(8);
        myX = myGroupStart;
    }

    void EndGroup() {
        ImFont* font = myUi.fonts.caption;
        const float fs = myUi.S(11);
        const float captionWidth = font->CalcTextSizeA(fs, FLT_MAX, 0, myCaption).x;
        const float content = std::max(myX - myGroupStart - myUi.S(2), captionWidth);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(font, fs, {myGroupStart + (content - captionWidth) / 2, myTop + myHeight - myUi.S(18)},
                    ui::ToU32(myUi.C().textMuted), myCaption);
        const float divider = myGroupStart + content + myUi.S(8);
        dl->AddLine({divider, myTop + myUi.S(6)}, {divider, myTop + myHeight - myUi.S(6)}, ui::ToU32(myUi.C().divider));
        myX = divider + myUi.S(1);
    }

    /// Large button running an action. `caption` may contain "\n".
    void Large(const char* actionId, const char* caption, bool active = false) {
        const Action* a = myApp.actions.Find(actionId);
        ImGui::SetCursorScreenPos({myX, myTop + myUi.S(6)});
        const std::string tip = Tooltip(*a);
        if (ui::RibbonLargeButton(myUi, actionId, a->icon, caption, tip.c_str(), a->IsEnabled(), active)) Run(actionId);
        myX += myUi.S(74);
    }

    void LargeLater(const char* icon, const char* caption) {
        ImGui::SetCursorScreenPos({myX, myTop + myUi.S(6)});
        ImGui::PushID(caption);
        ui::RibbonLargeButton(myUi, "##later", icon, caption, kLater, false);
        ImGui::PopID();
        myX += myUi.S(74);
    }

    void BeginColumn() {
        myColumnX = myX;
        myColumnY = myTop + myUi.S(8);
        myColumnWidth = 0;
    }

    void Small(const char* actionId, const char* label = nullptr, bool active = false) {
        const Action* a = myApp.actions.Find(actionId);
        ImGui::SetCursorScreenPos({myColumnX, myColumnY});
        const std::string tip = Tooltip(*a);
        if (ui::RibbonSmallButton(myUi, actionId, a->icon, label ? label : a->Label(), tip.c_str(), a->IsEnabled(), active))
            Run(actionId);
        NextRow();
    }

    void SmallLater(const char* icon, const char* label) {
        ImGui::SetCursorScreenPos({myColumnX, myColumnY});
        ImGui::PushID(label);
        ui::RibbonSmallButton(myUi, "##later", icon, label, kLater, false);
        ImGui::PopID();
        NextRow();
    }

    void EndColumn() { myX = myColumnX + myColumnWidth + myUi.S(2); }

    /// Icon-only buttons in rows of `columns`, as Inventor's constraint grid.
    void BeginGrid(int columns) {
        myGridColumns = columns;
        myGridIndex = 0;
        myColumnX = myX;
        myColumnY = myTop + myUi.S(8);
    }

    void Icon(const char* actionId, bool active = false) {
        const Action* a = myApp.actions.Find(actionId);
        const float cell = myUi.S(26);
        const int col = myGridIndex % myGridColumns, row = myGridIndex / myGridColumns;
        ImGui::SetCursorScreenPos({myColumnX + static_cast<float>(col) * cell, myColumnY + static_cast<float>(row) * myUi.S(24)});
        const std::string tip = Tooltip(*a);
        if (ui::IconButton(myUi, actionId, a->icon, 24, 18, tip.c_str(), a->IsEnabled(), active)) Run(actionId);
        ++myGridIndex;
    }

    void EndGrid() { myX = myColumnX + myUi.S(26) * static_cast<float>(myGridColumns) + myUi.S(2); }

private:
    void NextRow() {
        myColumnWidth = std::max(myColumnWidth, ImGui::GetItemRectSize().x);
        myColumnY += myUi.S(24);
    }

    static std::string Tooltip(const Action& a) {
        std::string tip = a.Label();
        if (const std::string sc = ActionRegistry::ChordText(a.chord); !sc.empty()) tip += " (" + sc + ")";
        if (!a.tooltip.empty()) tip += std::string("\n") + a.Tooltip();
        return tip;
    }

    void Run(const char* actionId) {
        const std::string id = actionId;
        AppContext& app = myApp;
        app.Later([&app, id] { app.actions.Run(id); });
    }

    AppContext& myApp;
    ui::Ui&     myUi;
    float       myTop;
    float       myHeight;
    float       myX = 0;
    float       myGroupStart = 0;
    int         myGridColumns = 1;
    int         myGridIndex = 0;
    const char* myCaption = "";
    float       myColumnX = 0, myColumnY = 0, myColumnWidth = 0;
};

void ModelRibbon(RibbonBuilder& r) {
    r.BeginGroup(Tr("Эскиз"));
    r.Large("sketch.start", Tr("Начать\nэскиз"));
    r.EndGroup();

    r.BeginGroup(Tr("Создать"));
    r.Large("feature.extrude", Tr("Выдав-\nливание"));
    r.Large("feature.revolve", Tr("Вращение"));
    r.BeginColumn();
    r.Small("feature.sweep");
    r.Small("feature.loft");
    r.Small("feature.coil");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Изменить"));
    r.Large("feature.hole", Tr("Отверстие"));
    r.Large("feature.fillet", Tr("Сопря-\nжение"));
    r.Large("feature.thread", Tr("Резьба"));
    r.BeginColumn();
    r.Small("feature.chamfer");
    r.Small("feature.shell");
    r.EndColumn();
    r.BeginColumn();
    r.Small("feature.section");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Массив"));
    r.BeginColumn();
    r.Small("feature.patternRect");
    r.Small("feature.patternCircular");
    r.Small("feature.mirror");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Раб. элементы"));
    r.BeginColumn();
    r.Small("feature.workPlane");
    r.Small("feature.workAxis");
    r.SmallLater("work-point", Tr("Точка"));
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Ст. изделия"));
    r.LargeLater("library", Tr("Библиотека"));
    r.EndGroup();

    r.BeginGroup(Tr("Экспорт"));
    r.BeginColumn();
    r.Small("file.exportStep", "STEP");
    r.Small("file.exportStl", "STL");
    r.Small("file.export3mf", "3MF");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Диагностика"));
    r.BeginColumn();
    r.Small("tools.measure", nullptr, r.App().shell.measuring);
    r.Small("view.section", nullptr, r.App().shell.section.on);
    r.EndColumn();
    r.BeginColumn();
    r.Small("tools.massProperties");
    r.Small("tools.interference", nullptr, r.App().shell.interference);
    r.EndColumn();
    r.EndGroup();
}

void SketchRibbon(AppContext& app, RibbonBuilder& r) {
    const bool sketching = app.Doc() && app.Doc()->mode == EditMode::Sketch;
    const SketchTool tool = app.sketch.Tool();
    const auto active = [&](SketchTool t) { return sketching && tool == t; };

    if (!sketching) {
        r.BeginGroup(Tr("Эскиз"));
        r.Large("sketch.start", Tr("Начать\nэскиз"));
        r.EndGroup();
    }
    r.BeginGroup(Tr("Создать"));
    r.Large("sketch.line", Tr("Отрезок"), active(SketchTool::Line));
    r.Large("sketch.circle", Tr("Окруж-\nность"), active(SketchTool::Circle));
    r.Large("sketch.arc", Tr("Дуга"), active(SketchTool::Arc));
    r.Large("sketch.rectangle", Tr("Прямо-\nугольник"), active(SketchTool::Rectangle));
    r.BeginColumn();
    r.Small("sketch.point", nullptr, active(SketchTool::Point));
    r.Small("sketch.spline", nullptr, active(SketchTool::Spline));
    r.Small("sketch.text", nullptr, active(SketchTool::Text));
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Модель"));
    const bool projecting = active(SketchTool::Project);
    r.Large("sketch.project", Tr("Проеци-\nровать"), projecting && app.sketch.ProjectsLinked());
    r.Large("sketch.projectFixed", Tr("Незави-\nсимая"), projecting && !app.sketch.ProjectsLinked());
    r.EndGroup();

    r.BeginGroup(Tr("Массив"));
    r.BeginColumn();
    r.Small("sketch.patternRect", nullptr, active(SketchTool::PatternRect));
    r.Small("sketch.patternCircular", nullptr, active(SketchTool::PatternCircular));
    r.Small("sketch.mirror", nullptr, active(SketchTool::Mirror));
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Изменить"));
    r.BeginColumn();
    r.Small("sketch.move", nullptr, active(SketchTool::Move));
    r.Small("sketch.rotate", nullptr, active(SketchTool::Rotate));
    r.EndColumn();
    r.BeginColumn();
    r.Small("sketch.copy", nullptr, active(SketchTool::Copy));
    r.Small("sketch.scale", nullptr, active(SketchTool::Scale));
    r.EndColumn();
    r.BeginColumn();
    r.Small("sketch.trim", nullptr, active(SketchTool::Trim));
    r.Small("sketch.extend", nullptr, active(SketchTool::Extend));
    r.EndColumn();
    r.BeginColumn();
    r.Small("sketch.fillet", nullptr, active(SketchTool::Fillet));
    r.Small("sketch.offset", nullptr, active(SketchTool::Offset));
    r.EndColumn();
    r.BeginColumn();
    r.Small("sketch.split", nullptr, active(SketchTool::Split));
    r.Small("edit.delete");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Зависимости"));
    r.BeginGrid(5);
    r.Icon("sketch.coincident", active(SketchTool::Coincident));
    r.Icon("sketch.horizontal", active(SketchTool::Horizontal));
    r.Icon("sketch.vertical", active(SketchTool::Vertical));
    r.Icon("sketch.fix", active(SketchTool::Fix));
    r.Icon("sketch.parallel", active(SketchTool::Parallel));
    r.Icon("sketch.perpendicular", active(SketchTool::Perpendicular));
    r.Icon("sketch.equal", active(SketchTool::Equal));
    r.Icon("sketch.midpoint", active(SketchTool::Midpoint));
    r.Icon("sketch.tangent", active(SketchTool::Tangent));
    r.Icon("sketch.concentric", active(SketchTool::Concentric));
    r.Icon("sketch.symmetric", active(SketchTool::Symmetric));
    r.Icon("sketch.showConstraints", r.App().settings.showAllConstraints);
    r.Icon("sketch.hideConstraints", !r.App().settings.showAllConstraints);
    r.EndGrid();
    r.EndGroup();

    r.BeginGroup(Tr("Размеры"));
    r.Large("sketch.dimension", Tr("Размер"), active(SketchTool::Dimension));
    r.EndGroup();

    r.BeginGroup(Tr("Формат"));
    r.BeginColumn();
    r.Small("sketch.construction");
    r.Small("sketch.centerline");
    r.Small("sketch.ortho", nullptr, r.App().settings.orthoMode);
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Выход"));
    r.Large("sketch.finish", Tr("Принять\nэскиз"));
    r.BeginColumn();
    r.Small("sketch.lookAt", Tr("На эскиз"));
    r.EndColumn();
    r.EndGroup();
}

void ViewRibbon(AppContext& app, RibbonBuilder& r) {
    const render::DisplayStyle style = app.shell.displayStyle;
    r.BeginGroup(Tr("Цвета"));
    r.Large("view.appearance", Tr("Внешний\nвид"), app.ops.appearance.IsOpen());
    r.Large("view.background", Tr("Фон"));
    r.EndGroup();

    r.BeginGroup(Tr("Стиль отображения"));
    r.BeginColumn();
    r.Small("view.shaded", nullptr, style == render::DisplayStyle::Shaded);
    r.Small("view.edges", nullptr, style == render::DisplayStyle::ShadedWithEdges);
    r.Small("view.wireframe", nullptr, style == render::DisplayStyle::Wireframe);
    r.EndColumn();
    r.BeginColumn();
    r.Small("view.hidden", nullptr, style == render::DisplayStyle::HiddenLines);
    r.Small("view.transparent", nullptr, style == render::DisplayStyle::Transparent);
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Ориентация"));
    r.Large("view.home", Tr("Домой"));
    r.Large("view.fit", Tr("Показать\nвсё"));
    r.BeginColumn();
    r.Small("view.front", Tr("Спереди"));
    r.Small("view.top", Tr("Сверху"));
    r.Small("view.right", Tr("Справа"));
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(Tr("Оформление"));
    r.BeginColumn();
    r.Small("view.themeSystem", Tr("Как в системе"), app.settings.theme == ThemeChoice::System);
    r.Small("view.themeLight", Tr("Светлая"), app.settings.theme == ThemeChoice::Light);
    r.Small("view.themeDark", Tr("Тёмная"), app.settings.theme == ThemeChoice::Dark);
    r.EndColumn();
    r.EndGroup();
}

} // namespace

void DrawModeBar(AppContext& app, ImVec2 pos, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float h = ui.S(ui.theme.metrics.modeBarHeight);
    ImDrawList* dl = BeginArea("##modebar", pos, {width, h}, c.appBackground);

    struct Tab {
        const char* label;
        RibbonTab   tab;
    };
    const Tab tabs[] = {{Tr("3D-модель"), RibbonTab::Model}, {Tr("Эскиз"), RibbonTab::Sketch}, {Tr("Вид"), RibbonTab::View}};
    float x = pos.x + ui.S(12);
    for (const Tab& t : tabs) {
        const bool on = app.shell.ribbonTab == t.tab;
        ImFont* font = on ? ui.fonts.strong : ui.fonts.medium;
        const ImVec2 ts = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, t.label);
        const float w = ts.x + ui.S(28);
        ImGui::SetCursorScreenPos({x, pos.y});
        ImGui::PushID(t.label);
        if (ImGui::InvisibleButton("##tab", {w, h})) app.shell.ribbonTab = t.tab;
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (on || hovered)
            dl->AddRectFilled({x, pos.y}, {x + w, pos.y + h}, ui::ToU32(on ? c.ribbon : c.hover), ui.S(6),
                              ImDrawFlags_RoundCornersTop);
        if (on) dl->AddRectFilled({x, pos.y + h - ui.S(2)}, {x + w, pos.y + h}, ui::ToU32(c.accent));
        dl->AddText(font, ui.S(13), {x + ui.S(14), pos.y + (h - ts.y) / 2 + ui.S(1)},
                    ui::ToU32(on ? c.text : c.textSecondary), t.label);
        x += w + ui.S(2);
    }
    EndArea();
}

float DrawRibbon(AppContext& app, Session& session, ImVec2 pos, float width) {
    (void)session;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float h = ui.S(100);
    ImDrawList* dl = BeginArea("##ribbon", pos, {width, h}, c.ribbon);
    dl->AddLine(pos, {pos.x + width, pos.y}, ui::ToU32(c.dividerSoft));
    dl->AddLine({pos.x, pos.y + h - 1}, {pos.x + width, pos.y + h - 1}, ui::ToU32(c.divider));

    // On a narrow window (a 1366-pixel laptop) the ribbon scrolls sideways:
    // mouse wheel over it, or the arrows at its edges. The arrows go first
    // (sized by the last frame's layout) so the buttons under them do not
    // take their clicks.
    float& scroll = app.shell.ribbonScroll;
    const float overflow = std::max(0.0f, app.shell.ribbonContent - width);
    scroll = std::clamp(scroll, 0.0f, overflow);
    const float aw = ui.S(22);
    const bool leftArrow = overflow > 0 && scroll > 0, rightArrow = overflow > 0 && scroll < overflow;
    const auto arrow = [&](bool right) {
        const ImVec2 a0{right ? pos.x + width - aw : pos.x, pos.y + 1}, a1{right ? pos.x + width : pos.x + aw, pos.y + h - 1};
        ImGui::SetCursorScreenPos(a0);
        ImGui::PushID(right ? "##ribbon-right" : "##ribbon-left");
        if (ImGui::InvisibleButton("##arrow", {aw, h - 2})) scroll += right ? width * 0.5f : -width * 0.5f;
        const bool hot = ImGui::IsItemHovered();
        ImGui::PopID();
        dl->AddRectFilled(a0, a1, ui::ToU32(hot ? c.hover : c.ribbon));
        dl->AddLine({right ? a0.x : a1.x, a0.y}, {right ? a0.x : a1.x, a1.y}, ui::ToU32(c.divider));
        const ImVec2 mid{(a0.x + a1.x) / 2, (a0.y + a1.y) / 2};
        const float s = ui.S(5);
        if (right) dl->AddTriangleFilled({mid.x - s / 2, mid.y - s}, {mid.x - s / 2, mid.y + s}, {mid.x + s / 2, mid.y}, ui::ToU32(c.textSecondary));
        else dl->AddTriangleFilled({mid.x + s / 2, mid.y - s}, {mid.x + s / 2, mid.y + s}, {mid.x - s / 2, mid.y}, ui::ToU32(c.textSecondary));
    };
    if (leftArrow) arrow(false);
    if (rightArrow) arrow(true);

    RibbonBuilder builder(app, {pos.x - scroll, pos.y}, h);
    ImGui::PushClipRect({pos.x + (leftArrow ? aw : 0), pos.y}, {pos.x + width - (rightArrow ? aw : 0), pos.y + h}, true);
    switch (app.shell.ribbonTab) {
        case RibbonTab::Model:  ModelRibbon(builder); break;
        case RibbonTab::Sketch: SketchRibbon(app, builder); break;
        case RibbonTab::View:   ViewRibbon(app, builder); break;
    }
    ImGui::PopClipRect();
    app.shell.ribbonContent = builder.End() + scroll - pos.x + ui.S(8);
    if (overflow > 0 && ImGui::IsMouseHoveringRect(pos, {pos.x + width, pos.y + h}))
        scroll -= (ImGui::GetIO().MouseWheel + ImGui::GetIO().MouseWheelH) * ui.S(60);
    EndArea();
    return h;
}

} // namespace bz::app
