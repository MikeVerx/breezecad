// Mode tabs and the ribbon. Commands that are not built yet are shown
// greyed out with a note, so the layout matches the approved mockup.

#include "app/features/OperationEditors.h"
#include "app/shell/Shell.h"
#include "app/sketch/SketchEditor.h"
#include "core/I18n.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

namespace bz::app {
namespace {

constexpr const char* kLater = N_("Появится в следующих версиях");

constexpr float kFoldSeconds = 0.5f;   ///< a group folding to icons or back

bool IconsOnly(const Settings& s, const std::string& key) {
    return std::find(s.ribbonIconsOnly.begin(), s.ribbonIconsOnly.end(), key) != s.ribbonIconsOnly.end();
}

void SetIconsOnly(Settings& s, const std::string& key, bool on) {
    std::erase(s.ribbonIconsOnly, key);
    if (on) s.ribbonIconsOnly.push_back(key);
}

/// Lays ribbon groups out left to right. A group with small buttons folds:
/// its captions go and only the icons stay (a drag of its right edge, or a
/// click on its name), so the ribbon takes less room for who knows the icons.
/// Large buttons keep their captions always.
class RibbonBuilder final : public RibbonSink {
public:
    RibbonBuilder(AppContext& app, ImVec2 origin, float height, std::string tabKey)
        : myApp(app), myUi(app.ui), myTop(origin.y), myHeight(height), myTabKey(std::move(tabKey)) {
        myX = origin.x + myUi.S(8);
        myApp.shell.ribbonFoldable.clear();
    }

    AppContext& App() const override { return myApp; }
    /// Right edge of what was laid out so far.
    float End() const { return myX; }

    void BeginGroup(const char* caption) override {
        myCaption = caption;
        myKey = myTabKey + ":" + caption;
        myFoldable = false;
        myShown = 1.0f - Eased(FoldProgress());
        myGroupStart = myX + myUi.S(8);
        myX = myGroupStart;
    }

    void EndGroup() override {
        ImFont* font = myUi.fonts.caption;
        const float fs = myUi.S(11);
        const char* caption = Tr(myCaption);
        const float captionWidth = font->CalcTextSizeA(fs, FLT_MAX, 0, caption).x;
        const float content = std::max(myX - myGroupStart - myUi.S(2), captionWidth);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 captionAt{myGroupStart + (content - captionWidth) / 2, myTop + myHeight - myUi.S(18)};
        const float divider = myGroupStart + content + myUi.S(8);
        bool hot = false;
        if (myFoldable) {
            myApp.shell.ribbonFoldable.push_back(myKey);
            hot = FoldControls(content, divider);
        }
        const ui::ThemeColors& c = myUi.C();
        dl->AddText(font, fs, captionAt, ui::ToU32(hot ? c.accent : c.textMuted), caption);
        if (hot) {   // which way a click goes: a small arrow after the name
            const float s = myUi.S(3), ax = captionAt.x + captionWidth + myUi.S(6), ay = captionAt.y + fs / 2 + myUi.S(1);
            const bool folded = IconsOnly(myApp.settings, myKey);
            if (folded) dl->AddTriangleFilled({ax - s / 2, ay - s}, {ax - s / 2, ay + s}, {ax + s / 2, ay}, ui::ToU32(c.accent));
            else dl->AddTriangleFilled({ax + s / 2, ay - s}, {ax + s / 2, ay + s}, {ax - s / 2, ay}, ui::ToU32(c.accent));
        }
        dl->AddLine({divider, myTop + myUi.S(6)}, {divider, myTop + myHeight - myUi.S(6)},
                    ui::ToU32(hot ? c.accent : c.divider), hot ? myUi.S(2) : 1.0f);
        myX = divider + myUi.S(1);
    }

    /// Large button running an action. `caption` may contain "\n".
    void Large(const char* actionId, const char* caption, bool active = false) override {
        const Action* a = myApp.actions.Find(actionId);
        ImGui::SetCursorScreenPos({myX, myTop + myUi.S(6)});
        const std::string tip = Tooltip(*a);
        if (ui::RibbonLargeButton(myUi, actionId, a->icon, caption, tip.c_str(), a->IsEnabled(), active)) Run(actionId);
        myX += myUi.S(74);
    }

    void LargeLater(const char* icon, const char* caption) override {
        ImGui::SetCursorScreenPos({myX, myTop + myUi.S(6)});
        ImGui::PushID(caption);
        ui::RibbonLargeButton(myUi, "##later", icon, caption, kLater, false);
        ImGui::PopID();
        myX += myUi.S(74);
    }

    void BeginColumn() override {
        myColumnX = myX;
        myColumnY = myTop + myUi.S(8);
        myColumnWidth = 0;
    }

    void Small(const char* actionId, const char* label = nullptr, bool active = false) override {
        const Action* a = myApp.actions.Find(actionId);
        ImGui::SetCursorScreenPos({myColumnX, myColumnY});
        const std::string tip = Tooltip(*a);
        if (ui::RibbonSmallButton(myUi, actionId, a->icon, label ? label : a->Label(), tip.c_str(), a->IsEnabled(), active, myShown))
            Run(actionId);
        myFoldable = true;
        NextRow();
    }

    void SmallLater(const char* icon, const char* label) override {
        ImGui::SetCursorScreenPos({myColumnX, myColumnY});
        ImGui::PushID(label);
        const std::string tip = std::string(label) + "\n" + Tr(kLater);
        ui::RibbonSmallButton(myUi, "##later", icon, label, tip.c_str(), false, false, myShown);
        ImGui::PopID();
        myFoldable = true;
        NextRow();
    }

    void EndColumn() override { myX = myColumnX + myColumnWidth + myUi.S(2); }

    /// The constraints: small buttons three to a column, like the rest, so
    /// they fold the same way (folded, they are Inventor's icon grid).
    void BeginGrid(int /*columns*/) override {
        myGridIndex = 0;
        BeginColumn();
    }

    void Icon(const char* actionId, bool active = false) override {
        if (myGridIndex > 0 && myGridIndex % kGridRows == 0) {
            EndColumn();
            BeginColumn();
        }
        Small(actionId, nullptr, active);
        ++myGridIndex;
    }

    void EndGrid() override { EndColumn(); }

private:
    static constexpr int kGridRows = 3;

    static float Eased(float t) { return t * t * (3.0f - 2.0f * t); }

    /// This group's fold, 0 … 1, a step nearer to what the settings say.
    float FoldProgress() {
        const float goal = IconsOnly(myApp.settings, myKey) ? 1.0f : 0.0f;
        float& p = myApp.shell.ribbonFold.try_emplace(myKey, goal).first->second;
        if (p != goal) {
            // After a long wait for input the first frame's time is long: no jump.
            const float dt = std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
            const float step = myApp.settings.animations ? dt / kFoldSeconds : 1.0f;
            p = goal > p ? std::min(goal, p + step) : std::max(goal, p - step);
            myApp.shell.animateUi = true;   // keep drawing frames until it is there
        }
        return p;
    }

    /// The group's name (a click folds or unfolds) and its right edge (a drag
    /// left folds, right unfolds). True while either is under the mouse.
    bool FoldControls(float content, float divider) {
        ShellState& shell = myApp.shell;
        const bool folded = IconsOnly(myApp.settings, myKey);
        const float mouseX = ImGui::GetIO().MousePos.x;
        ImGui::PushID(myKey.c_str());

        ImGui::SetCursorScreenPos({myGroupStart, myTop + myHeight - myUi.S(20)});
        const bool captionClicked = ImGui::InvisibleButton("##fold-caption", {content, myUi.S(18)});
        bool hot = ImGui::IsItemHovered();
        if (hot) ui::ItemTooltip(myUi, folded ? Tr("Показать подписи") : Tr("Только иконки"));
        if (captionClicked) SetIconsOnly(myApp.settings, myKey, !folded);

        ImGui::SetCursorScreenPos({divider - myUi.S(4), myTop + myUi.S(6)});
        ImGui::InvisibleButton("##fold-edge", {myUi.S(8), myHeight - myUi.S(12)});
        if (ImGui::IsItemActivated()) {
            shell.ribbonEdgeX = mouseX;
            shell.ribbonEdgeMoved = false;
        }
        if (ImGui::IsItemActive()) {
            const float dx = mouseX - shell.ribbonEdgeX;
            if (std::abs(dx) > myUi.S(4)) shell.ribbonEdgeMoved = true;
            const bool now = IconsOnly(myApp.settings, myKey);
            if ((dx < -myUi.S(24) && !now) || (dx > myUi.S(24) && now)) {
                SetIconsOnly(myApp.settings, myKey, !now);
                shell.ribbonEdgeX = mouseX;
            }
        }
        if (ImGui::IsItemDeactivated() && !shell.ribbonEdgeMoved) SetIconsOnly(myApp.settings, myKey, !folded);
        const bool edgeHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (edgeHot) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (!ImGui::IsItemActive()) ui::ItemTooltip(myUi, Tr("Тяните влево — только иконки, вправо — с подписями"));
        }
        ImGui::PopID();
        return hot || edgeHot;
    }

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
    std::string myTabKey;
    std::string myKey;              ///< "sketch:Изменить": the group in the settings
    bool        myFoldable = false; ///< the group has small buttons
    float       myShown = 1.0f;     ///< how much of the small buttons' captions shows
    int         myGridIndex = 0;
    const char* myCaption = "";
    float       myColumnX = 0, myColumnY = 0, myColumnWidth = 0;
};

void ModelRibbonImpl(RibbonSink& r) {
    r.BeginGroup(N_("Эскиз"));
    r.Large("sketch.start", Tr("Начать\nэскиз"));
    r.EndGroup();

    r.BeginGroup(N_("Создать"));
    r.Large("feature.extrude", Tr("Выдав-\nливание"));
    r.Large("feature.revolve", Tr("Вращение"));
    r.BeginColumn();
    r.Small("feature.sweep");
    r.Small("feature.loft");
    r.Small("feature.coil");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Изменить"));
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

    r.BeginGroup(N_("Массив"));
    r.BeginColumn();
    r.Small("feature.patternRect");
    r.Small("feature.patternCircular");
    r.Small("feature.mirror");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Раб. элементы"));
    r.BeginColumn();
    r.Small("feature.workPlane");
    r.Small("feature.workAxis");
    r.Small("feature.workPoint");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Ст. изделия"));
    r.Large("parts.library", Tr("Библиотека"));
    r.EndGroup();

    r.BeginGroup(N_("Экспорт"));
    r.BeginColumn();
    r.Small("file.exportStep", "STEP");
    r.Small("file.exportStl", "STL");
    r.Small("file.export3mf", "3MF");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Диагностика"));
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

void AssemblyRibbonImpl(AppContext& app, RibbonSink& r) {
    const AssemblyState::Tool tool = app.Doc() && app.Doc()->assembly ? app.Doc()->assembly->tool : AssemblyState::Tool::Move;
    r.BeginGroup(N_("Компонент"));
    r.Large("assembly.place", Tr("Вставить"));
    r.Large("assembly.create", Tr("Создать"));
    r.Large("assembly.placeStandard", Tr("Стандартное\nизделие"));
    r.BeginColumn();
    r.Small("assembly.pattern");
    r.Small("assembly.editInPlace");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Зависимости"));
    r.Large("assembly.mate", Tr("Совме-\nщение"));
    r.Large("assembly.insert", Tr("Вставка"));
    r.BeginColumn();
    r.Small("assembly.angle");
    r.Small("assembly.tangent");
    r.Small("assembly.axisMate");
    r.EndColumn();
    r.BeginColumn();
    r.Small("assembly.symmetry");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Соединения"));
    r.Large("assembly.jointRotational", Tr("Враще-\nние"));
    r.BeginColumn();
    r.Small("assembly.jointSlider");
    r.Small("assembly.jointCylindrical");
    r.Small("assembly.jointPlanar");
    r.EndColumn();
    r.BeginColumn();
    r.Small("assembly.jointBall");
    r.Small("assembly.jointRigid");
    r.EndColumn();
    r.BeginColumn();
    r.Small("assembly.gear");
    r.Small("assembly.screw");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Положение"));
    r.BeginColumn();
    r.Small("assembly.move", nullptr, tool == AssemblyState::Tool::Move);
    r.Small("assembly.rotate", nullptr, tool == AssemblyState::Tool::Rotate);
    r.Small("assembly.ground");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Экспорт"));
    r.BeginColumn();
    r.Small("file.exportStep", "STEP");
    r.Small("file.exportStl", "STL");
    r.Small("file.export3mf", "3MF");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Диагностика"));
    r.BeginColumn();
    r.Small("tools.measure", nullptr, app.shell.measuring);
    r.Small("view.section", nullptr, app.shell.section.on);
    r.Small("tools.interference", nullptr, app.shell.interference);
    r.EndColumn();
    r.EndGroup();
}

void SketchRibbonImpl(AppContext& app, RibbonSink& r, bool withFinish) {
    const bool sketching = app.Doc() && app.Doc()->mode == EditMode::Sketch;
    const SketchTool tool = app.sketch.Tool();
    const auto active = [&](SketchTool t) { return sketching && tool == t; };

    if (!sketching) {
        r.BeginGroup(N_("Эскиз"));
        r.Large("sketch.start", Tr("Начать\nэскиз"));
        r.EndGroup();
    }
    r.BeginGroup(N_("Создать"));
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

    r.BeginGroup(N_("Модель"));
    const bool projecting = active(SketchTool::Project);
    r.Large("sketch.project", Tr("Проеци-\nровать"), projecting && app.sketch.ProjectsLinked());
    r.Large("sketch.projectFixed", Tr("Незави-\nсимая"), projecting && !app.sketch.ProjectsLinked());
    r.EndGroup();

    r.BeginGroup(N_("Массив"));
    r.BeginColumn();
    r.Small("sketch.patternRect", nullptr, active(SketchTool::PatternRect));
    r.Small("sketch.patternCircular", nullptr, active(SketchTool::PatternCircular));
    r.Small("sketch.mirror", nullptr, active(SketchTool::Mirror));
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Изменить"));
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

    r.BeginGroup(N_("Зависимости"));
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

    r.BeginGroup(N_("Размеры"));
    r.Large("sketch.dimension", Tr("Размер"), active(SketchTool::Dimension));
    r.BeginColumn();
    r.Small("sketch.arrangeDimensions");
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Формат"));
    r.BeginColumn();
    r.Small("sketch.construction");
    r.Small("sketch.centerline");
    r.Small("sketch.ortho", nullptr, r.App().settings.orthoMode);
    r.EndColumn();
    r.EndGroup();

    // "На эскиз" closes the tools, large; "Принять эскиз" is pinned at an end
    // of the ribbon while sketching (DrawRibbon), here only otherwise.
    r.BeginGroup(N_("Вид"));
    r.Large("sketch.lookAt", Tr("На эскиз"));
    r.EndGroup();

    if (withFinish) {
        r.BeginGroup(N_("Выход"));
        r.Large("sketch.finish", Tr("Принять\nэскиз"));
        r.EndGroup();
    }
}

void ViewRibbonImpl(AppContext& app, RibbonSink& r) {
    const render::DisplayStyle style = app.shell.displayStyle;
    r.BeginGroup(N_("Цвета"));
    r.Large("view.appearance", Tr("Внешний\nвид"), app.ops.appearance.IsOpen());
    r.Large("view.background", Tr("Фон"));
    r.EndGroup();

    r.BeginGroup(N_("Стиль отображения"));
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

    r.BeginGroup(N_("Ориентация"));
    r.Large("view.home", Tr("Домой"));
    r.Large("view.fit", Tr("Показать\nвсё"));
    r.BeginColumn();
    r.Small("view.front", Tr("Спереди"));
    r.Small("view.top", Tr("Сверху"));
    r.Small("view.right", Tr("Справа"));
    r.EndColumn();
    r.EndGroup();

    r.BeginGroup(N_("Оформление"));
    r.BeginColumn();
    r.Small("view.themeSystem", Tr("Как в системе"), app.settings.theme == ThemeChoice::System);
    r.Small("view.themeLight", Tr("Светлая"), app.settings.theme == ThemeChoice::Light);
    r.Small("view.themeDark", Tr("Тёмная"), app.settings.theme == ThemeChoice::Dark);
    r.EndColumn();
    r.EndGroup();
}

} // namespace

void FillRibbon(AppContext& app, Session& session, RibbonSink& sink, bool withFinish) {
    switch (app.shell.ribbonTab) {
        case RibbonTab::Model:
            if (session.assembly) {
                AssemblyRibbonImpl(app, sink);
            } else {
                // A part edited in place: the way back to its assembly comes first.
                if (session.host) {
                    sink.BeginGroup(N_("Сборка"));
                    sink.Large("assembly.return", Tr("Вернуться"));
                    sink.EndGroup();
                }
                ModelRibbonImpl(sink);
            }
            break;
        case RibbonTab::Sketch: SketchRibbonImpl(app, sink, withFinish); break;
        case RibbonTab::View:   ViewRibbonImpl(app, sink); break;
    }
}

void DrawModeBar(AppContext& app, ImVec2 pos, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float h = ui.S(ui.theme.metrics.modeBarHeight);
    ImDrawList* dl = BeginArea("##modebar", pos, {width, h}, c.appBackground);

    struct Tab {
        const char* label;
        RibbonTab   tab;
    };
    const bool assemblyTabs = app.Doc() && app.Doc()->assembly && !app.workspace.ShowsHome();
    const Tab partTabs[] = {{Tr("3D-модель"), RibbonTab::Model}, {Tr("Эскиз"), RibbonTab::Sketch}, {Tr("Вид"), RibbonTab::View}};
    const Tab asmTabs[] = {{Tr("Сборка"), RibbonTab::Model}, {Tr("Вид"), RibbonTab::View}};
    if (assemblyTabs && app.shell.ribbonTab == RibbonTab::Sketch) app.shell.ribbonTab = RibbonTab::Model;
    float x = pos.x + ui.S(12);
    for (const Tab& t : assemblyTabs ? std::span<const Tab>(asmTabs) : std::span<const Tab>(partTabs)) {
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
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float h = ui.S(100);
    ImDrawList* dl = BeginArea("##ribbon", pos, {width, h}, c.ribbon);
    dl->AddLine(pos, {pos.x + width, pos.y}, ui::ToU32(c.dividerSoft));
    dl->AddLine({pos.x, pos.y + h - 1}, {pos.x + width, pos.y + h - 1}, ui::ToU32(c.divider));

    // "Принять эскиз" is pinned at an end of the ribbon in a sketch, whatever
    // the ribbon's scroll (ADR-0004: in every version), in the soft accent.
    // Which end is the user's: drag the button to the other one (or the
    // ribbon's menu); only the two ends, nothing between.
    const bool  pinFinish = session.mode == EditMode::Sketch && app.shell.ribbonTab == RibbonTab::Sketch;
    const bool  onRight = app.settings.finishOnRight;
    const float slot = ui.S(8 + 74 + 4);
    const float pinLeft = pinFinish && !onRight ? slot : 0.0f, pinRight = pinFinish && onRight ? slot : 0.0f;
    if (!pinFinish) app.shell.finishDrag = false;
    if (pinFinish) {
        const Action* finish = app.actions.Find("sketch.finish");
        const float bx = onRight ? pos.x + width - pinRight + ui.S(6) : pos.x + ui.S(8);
        ImGui::SetCursorScreenPos({bx, pos.y + ui.S(6)});
        std::string tip = finish->Label();
        if (const std::string sc = ActionRegistry::ChordText(finish->chord); !sc.empty()) tip += " (" + sc + ")";
        if (!app.shell.finishDrag) tip += std::string("\n") + Tr("Можно перетащить к другому краю ленты");
        const bool wasDragging = app.shell.finishDrag;
        const bool clicked =
            ui::RibbonLargeButton(ui, "##pinned-finish", finish->icon, Tr("Принять\nэскиз"), tip.c_str(), finish->IsEnabled(), true);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, ui.S(6))) app.shell.finishDrag = true;
        if (clicked && !wasDragging) app.Later([&app] { app.actions.Run("sketch.finish"); });   // a drop is not a click
        const float divider = onRight ? pos.x + width - pinRight : pos.x + pinLeft;
        dl->AddLine({divider, pos.y + ui.S(6)}, {divider, pos.y + h - ui.S(6)}, ui::ToU32(c.divider));
    }
    const float start = pos.x + pinLeft, end = pos.x + width - pinRight, span = end - start;

    // On a narrow window (a 1366-pixel laptop) the ribbon scrolls sideways:
    // mouse wheel over it, or the arrows at its edges. The arrows go first
    // (sized by the last frame's layout) so the buttons under them do not
    // take their clicks.
    float& scroll = app.shell.ribbonScroll;
    const float overflow = std::max(0.0f, app.shell.ribbonContent - span);
    scroll = std::clamp(scroll, 0.0f, overflow);
    const float aw = ui.S(22);
    const bool leftArrow = overflow > 0 && scroll > 0, rightArrow = overflow > 0 && scroll < overflow;
    const auto arrow = [&](bool right) {
        const ImVec2 a0{right ? end - aw : start, pos.y + 1}, a1{right ? end : start + aw, pos.y + h - 1};
        ImGui::SetCursorScreenPos(a0);
        ImGui::PushID(right ? "##ribbon-right" : "##ribbon-left");
        if (ImGui::InvisibleButton("##arrow", {aw, h - 2})) scroll += right ? span * 0.5f : -span * 0.5f;
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

    const char* tabKey = session.assembly ? "asm" : app.shell.ribbonTab == RibbonTab::Sketch ? "sketch"
                       : app.shell.ribbonTab == RibbonTab::View ? "view" : "model";
    RibbonBuilder builder(app, {start - scroll, pos.y}, h, tabKey);
    ImGui::PushClipRect({start + (leftArrow ? aw : 0), pos.y}, {end - (rightArrow ? aw : 0), pos.y + h}, true);
    FillRibbon(app, session, builder, !pinFinish);
    ImGui::PopClipRect();
    app.shell.ribbonContent = builder.End() + scroll - start + ui.S(8);
    const bool overRibbon = ImGui::IsMouseHoveringRect(pos, {pos.x + width, pos.y + h});
    if (overflow > 0 && overRibbon)
        scroll -= (ImGui::GetIO().MouseWheel + ImGui::GetIO().MouseWheelH) * ui.S(60);

    // Carrying "Принять эскиз": both ends show where it can go, the nearer
    // one stronger; it lands there when the button is let go.
    if (app.shell.finishDrag) {
        const float mx = ImGui::GetIO().MousePos.x;
        const bool toRight = mx > pos.x + width / 2;
        const auto zone = [&](bool right) {
            const ImVec2 z0{right ? pos.x + width - slot + ui.S(2) : pos.x + ui.S(4), pos.y + ui.S(4)};
            const ImVec2 z1{z0.x + slot - ui.S(6), pos.y + h - ui.S(4)};
            const bool closer = right == toRight;
            dl->AddRectFilled(z0, z1, ui::ToU32(c.accent.WithAlpha(closer ? 0.12f : 0.04f)), ui.S(8));
            dl->AddRect(z0, z1, ui::ToU32(c.accent.WithAlpha(closer ? 0.9f : 0.35f)), ui.S(8), 0, ui.S(closer ? 2.0f : 1.0f));
        };
        zone(false);
        zone(true);
        // The button follows the mouse, over everything.
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const float gw = ui.S(76), gh = ui.S(76);
        const ImVec2 g0{mx - gw / 2, pos.y + ui.S(8)}, g1{g0.x + gw, g0.y + gh};
        fg->AddRectFilled({g0.x, g0.y + ui.S(3)}, {g1.x, g1.y + ui.S(3)}, ui::ToU32(Color{0, 0, 0, ui.theme.dark ? 0.35f : 0.12f}), ui.S(8));
        fg->AddRectFilled(g0, g1, ui::ToU32(c.ribbon), ui.S(8));
        fg->AddRect(g0, g1, ui::ToU32(c.accent), ui.S(8), 0, ui.S(1.5f));
        const float is = ui.S(32);
        ui.icons.Draw(fg, app.actions.Find("sketch.finish")->icon, {g0.x + (gw - is) / 2, g0.y + ui.S(8)}, is, c.textSecondary);
        const char* label = Tr("Принять эскиз");
        const float fs = ui.S(11.5f);
        const float tw = ui.fonts.medium->CalcTextSizeA(fs, FLT_MAX, 0, label).x;
        fg->AddText(ui.fonts.medium, fs, {g0.x + (gw - tw) / 2, g0.y + ui.S(48)}, ui::ToU32(c.text), label);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            app.settings.finishOnRight = toRight;
            app.shell.finishDrag = false;
        }
    }

    // The ribbon's own menu (right click on it): captions for all its groups
    // or none, and where "Принять эскиз" stays.
    if (overRibbon && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        ImGui::OpenPopup("##ribbon-menu");
    {
        ui::MenuStyle style(ui);   // before Begin: must outlive the End call
        if (ImGui::BeginPopup("##ribbon-menu")) {
            std::vector<std::string>& folded = app.settings.ribbonIconsOnly;
            const std::vector<std::string>& here = app.shell.ribbonFoldable;
            const bool anyFolded = std::any_of(here.begin(), here.end(), [&](const std::string& k) { return IconsOnly(app.settings, k); });
            const bool anyOpen = std::any_of(here.begin(), here.end(), [&](const std::string& k) { return !IconsOnly(app.settings, k); });
            if (ui::MenuItem(ui, Tr("Показать все подписи"), nullptr, {}, false, anyFolded))
                for (const std::string& k : here) SetIconsOnly(app.settings, k, false);
            if (ui::MenuItem(ui, Tr("Только иконки"), nullptr, {}, false, anyOpen))
                for (const std::string& k : here) SetIconsOnly(app.settings, k, true);
            ImGui::Separator();
            if (ui::MenuItem(ui, Tr("«Принять эскиз» слева"), nullptr, {}, !app.settings.finishOnRight)) app.settings.finishOnRight = false;
            if (ui::MenuItem(ui, Tr("«Принять эскиз» справа"), nullptr, {}, app.settings.finishOnRight)) app.settings.finishOnRight = true;
            ImGui::Separator();
            if (ui::MenuItem(ui, Tr("Лента как по умолчанию"))) {
                folded = Settings{}.ribbonIconsOnly;
                app.settings.finishOnRight = Settings{}.finishOnRight;
            }
            ImGui::EndPopup();
        }
    }
    EndArea();
    return h;
}

} // namespace bz::app
