// Mode tabs and the ribbon. Commands that are not built yet are shown
// greyed out with a note, so the layout matches the approved mockup.

#include "app/sketch/SketchEditor.h"
#include "app/shell/Shell.h"
#include "ui/Widgets.h"

#include <algorithm>

namespace bz::app {
namespace {

constexpr const char* kLater = "Появится в следующих версиях";

/// Lays ribbon groups out left to right.
class RibbonBuilder {
public:
    RibbonBuilder(AppContext& app, ImVec2 origin, float height) : myApp(app), myUi(app.ui), myTop(origin.y), myHeight(height) {
        myX = origin.x + myUi.S(8);
    }

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
        if (ui::RibbonSmallButton(myUi, actionId, a->icon, label ? label : a->label.c_str(), tip.c_str(), a->IsEnabled(), active))
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

private:
    void NextRow() {
        myColumnWidth = std::max(myColumnWidth, ImGui::GetItemRectSize().x);
        myColumnY += myUi.S(24);
    }

    static std::string Tooltip(const Action& a) {
        std::string tip = a.label;
        if (const std::string sc = ActionRegistry::ChordText(a.chord); !sc.empty()) tip += " (" + sc + ")";
        if (!a.tooltip.empty()) tip += "\n" + a.tooltip;
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
    const char* myCaption = "";
    float       myColumnX = 0, myColumnY = 0, myColumnWidth = 0;
};

void ModelRibbon(RibbonBuilder& r) {
    r.BeginGroup("Эскиз");
    r.Large("sketch.start", "Начать\nэскиз");
    r.EndGroup();

    r.BeginGroup("Создать");
    r.Large("feature.extrude", "Выдав-\nливание");
    r.Large("feature.revolve", "Вращение");
    r.BeginColumn();
    r.SmallLater("sweep", "Сдвиг");
    r.SmallLater("loft", "Лофт");
    r.SmallLater("coil", "Пружина");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup("Изменить");
    r.Large("feature.hole", "Отверстие");
    r.Large("feature.fillet", "Сопря-\nжение");
    r.LargeLater("thread", "Резьба");
    r.BeginColumn();
    r.Small("feature.chamfer");
    r.Small("feature.shell");
    r.SmallLater("draft", "Уклон");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup("Массив");
    r.BeginColumn();
    r.SmallLater("pattern-rect", "Прямоугольный");
    r.SmallLater("pattern-circular", "Круговой");
    r.SmallLater("mirror", "Зеркало");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup("Раб. элементы");
    r.BeginColumn();
    r.Small("feature.workPlane");
    r.SmallLater("work-axis", "Ось");
    r.SmallLater("work-point", "Точка");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup("Ст. изделия");
    r.LargeLater("library", "Библиотека");
    r.EndGroup();

    r.BeginGroup("Экспорт");
    r.BeginColumn();
    r.Small("file.exportStep", "STEP");
    r.Small("file.exportStl", "STL");
    r.Small("file.export3mf", "3MF");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup("Диагностика");
    r.BeginColumn();
    r.SmallLater("measure", "Измерить");
    r.SmallLater("section", "Сечение");
    r.EndColumn();
    r.BeginColumn();
    r.Small("tools.massProperties");
    r.SmallLater("interference", "Коллизии");
    r.EndColumn();
    r.EndGroup();
}

void SketchRibbon(AppContext& app, RibbonBuilder& r) {
    const bool sketching = app.Doc() && app.Doc()->mode == EditMode::Sketch;
    const SketchTool tool = app.sketch.Tool();
    const auto active = [&](SketchTool t) { return sketching && tool == t; };

    if (!sketching) {
        r.BeginGroup("Эскиз");
        r.Large("sketch.start", "Начать\nэскиз");
        r.EndGroup();
    }
    r.BeginGroup("Рисование");
    r.Large("sketch.line", "Отрезок", active(SketchTool::Line));
    r.Large("sketch.rectangle", "Прямо-\nугольник", active(SketchTool::Rectangle));
    r.Large("sketch.circle", "Окруж-\nность", active(SketchTool::Circle));
    r.Large("sketch.arc", "Дуга", active(SketchTool::Arc));
    r.BeginColumn();
    r.Small("sketch.point", nullptr, active(SketchTool::Point));
    r.Small("sketch.construction");
    r.Small("edit.delete");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup("Ограничения");
    r.BeginColumn();
    r.Small("sketch.coincident", nullptr, active(SketchTool::Coincident));
    r.Small("sketch.horizontal", nullptr, active(SketchTool::Horizontal));
    r.Small("sketch.vertical", nullptr, active(SketchTool::Vertical));
    r.EndColumn();
    r.BeginColumn();
    r.Small("sketch.parallel", nullptr, active(SketchTool::Parallel));
    r.Small("sketch.perpendicular", nullptr, active(SketchTool::Perpendicular));
    r.Small("sketch.equal", nullptr, active(SketchTool::Equal));
    r.EndColumn();
    r.BeginColumn();
    r.Small("sketch.tangent", nullptr, active(SketchTool::Tangent));
    r.Small("sketch.midpoint", nullptr, active(SketchTool::Midpoint));
    r.Small("sketch.concentric", nullptr, active(SketchTool::Concentric));
    r.EndColumn();
    r.BeginColumn();
    r.Small("sketch.fix", nullptr, active(SketchTool::Fix));
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup("Размеры");
    r.Large("sketch.dimension", "Размер", active(SketchTool::Dimension));
    r.EndGroup();

    r.BeginGroup("Вид");
    r.Large("sketch.lookAt", "Смотреть\nна эскиз");
    r.EndGroup();

    r.BeginGroup("Выход");
    r.Large("sketch.finish", "Принять\nэскиз");
    r.EndGroup();
}

void ViewRibbon(AppContext& app, RibbonBuilder& r) {
    const render::DisplayStyle style = app.shell.displayStyle;
    r.BeginGroup("Стиль отображения");
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

    r.BeginGroup("Ориентация");
    r.Large("view.home", "Домой");
    r.Large("view.fit", "Показать\nвсё");
    r.BeginColumn();
    r.Small("view.front", "Спереди");
    r.Small("view.top", "Сверху");
    r.Small("view.right", "Справа");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup("Оформление");
    r.BeginColumn();
    r.Small("view.themeSystem", "Как в системе", app.settings.theme == ThemeChoice::System);
    r.Small("view.themeLight", "Светлая", app.settings.theme == ThemeChoice::Light);
    r.Small("view.themeDark", "Тёмная", app.settings.theme == ThemeChoice::Dark);
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
    const Tab tabs[] = {{"3D-модель", RibbonTab::Model}, {"Эскиз", RibbonTab::Sketch}, {"Вид", RibbonTab::View}};
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

    RibbonBuilder builder(app, pos, h);
    switch (app.shell.ribbonTab) {
        case RibbonTab::Model:  ModelRibbon(builder); break;
        case RibbonTab::Sketch: SketchRibbon(app, builder); break;
        case RibbonTab::View:   ViewRibbon(app, builder); break;
    }
    EndArea();
    return h;
}

} // namespace bz::app
