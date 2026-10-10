// Every user command, registered once. Ribbon, menus, the command search and
// keyboard shortcuts all run actions from here.

#include "app/Application.h"
#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "core/I18n.h"
#include "document/Origin.h"

namespace bz::app {
namespace {

ImGuiKeyChord Ctrl(ImGuiKey key) { return ImGuiMod_Ctrl | key; }
ImGuiKeyChord CtrlShift(ImGuiKey key) { return ImGuiMod_Ctrl | ImGuiMod_Shift | key; }

} // namespace

void RegisterActions(AppContext& app) {
    ActionRegistry& a = app.actions;
    const auto hasDoc = [&app] { return app.Doc() != nullptr && !app.workspace.ShowsHome(); };
    const auto inSketch = [&app] { return app.Doc() && app.Doc()->mode == EditMode::Sketch; };
    const auto inModel = [&app] { return app.Doc() && !app.workspace.ShowsHome() && app.Doc()->mode != EditMode::Sketch; };

    // --- file ---
    a.Add({"file.new", N_("Новая деталь"), "ui-new", Ctrl(ImGuiKey_N), {}, {}, [&app] { NewDocument(app); }});
    a.Add({"file.open", N_("Открыть…"), "ui-open", Ctrl(ImGuiKey_O), {}, {}, [&app] { OpenDocument(app); }});
    a.Add({"file.save", N_("Сохранить"), "ui-save", Ctrl(ImGuiKey_S), {}, hasDoc, [&app] { SaveDocument(app, false); }});
    a.Add({"file.saveAs", N_("Сохранить как…"), "ui-save", CtrlShift(ImGuiKey_S), {}, hasDoc, [&app] { SaveDocument(app, true); }});
    a.Add({"file.close", N_("Закрыть документ"), "ui-close", Ctrl(ImGuiKey_W), {}, hasDoc,
           [&app] { RequestClose(app, app.workspace.ActiveIndex()); }});
    a.Add({"file.exportStep", N_("Экспорт в STEP"), "export", 0, N_("Для обмена с другими САПР (AP214)"), hasDoc,
           [&app] { ExportDocument(app, ExportFormat::Step); }});
    a.Add({"file.exportStl", N_("Экспорт в STL"), "export", 0, N_("Сетка для 3D-печати"), hasDoc,
           [&app] { ExportDocument(app, ExportFormat::Stl); }});
    a.Add({"file.export3mf", N_("Экспорт в 3MF"), "export", 0, N_("Современный формат для слайсеров"), hasDoc,
           [&app] { ExportDocument(app, ExportFormat::ThreeMf); }});
    a.Add({"app.quit", N_("Выход"), "", Ctrl(ImGuiKey_Q), {}, {}, [&app] { app.shell.quitRequested = true; }});
    a.Add({"app.home", N_("Главная страница"), "ui-home", 0, {}, {}, [&app] { app.workspace.ShowHome(true); }});
    a.Add({"app.search", N_("Поиск команд"), "ui-search", Ctrl(ImGuiKey_K), {}, {}, [&app] {
               app.shell.paletteOpen = true;
               app.shell.focusPalette = true;
           }});
    a.Add({"tools.measure", N_("Измерить"), "measure", 0, N_("Размеры элемента и расстояние между двумя (Esc — закончить)"),
           [&app] { return app.Doc() && !app.workspace.ShowsHome() && !app.Doc()->model.Bodies().empty(); }, [&app] {
               if (Session* s = app.Doc()) app.ops.Close(app, *s);
               app.shell.measuring = !app.shell.measuring;
           }});
    a.Add({"tools.interference", N_("Коллизии"), "interference", 0, N_("Показать красным, где тела детали заходят друг в друга"),
           [&app] { return app.Doc() && !app.workspace.ShowsHome() && !app.Doc()->model.Bodies().empty(); },
           [&app] { ToggleInterference(app); }});
    a.Add({"view.appearance", N_("Внешний вид"), "appearance", 0,
           N_("Цвет и материал тел и граней: металлы, пластики, свои цвета с прозрачностью"),
           [&app] { return app.Doc() && !app.workspace.ShowsHome() && !app.Doc()->model.Bodies().empty(); },
           [&app] { app.ops.appearance.Start(app, *app.Doc()); }});
    a.Add({"view.background", N_("Фон"), "view-background", 0, N_("Фон рабочей области: градиенты или своя картинка"), {},
           [&app] { app.shell.backgroundMenu = true; }});
    a.Add({"view.section", N_("Разрез вида"), "section", 0, N_("Разрезать вид плоскостью, чтобы заглянуть внутрь детали"),
           [&app] { return app.Doc() && !app.workspace.ShowsHome() && !app.Doc()->model.Bodies().empty(); },
           [&app] { app.shell.section.on = !app.shell.section.on; }});
    a.Add({"tools.massProperties", N_("МЦХ"), "mass-properties", 0, N_("Масса, объём, площадь, центр масс и габариты детали"),
           [&app] { return app.Doc() && !app.workspace.ShowsHome() && !app.Doc()->model.Bodies().empty(); },
           [&app] { app.shell.showMassProperties = true; }});
    a.Add({"app.settings", N_("Настройки"), "ui-sliders", 0, N_("Масштаб интерфейса, оформление, сочетания клавиш"), {},
           [&app] { app.shell.showSettings = true; }});
    a.Add({"app.about", N_("О программе"), "ui-info", 0, {}, {}, [&app] { app.shell.showAbout = true; }});

    // --- edit ---
    a.Add({"edit.undo", N_("Отменить"), "ui-undo", Ctrl(ImGuiKey_Z), {}, [&app] { return CanUndo(app); }, [&app] { Undo(app); }});
    a.Add({"edit.redo", N_("Повторить"), "ui-redo", Ctrl(ImGuiKey_Y), {}, [&app] { return CanRedo(app); }, [&app] { Redo(app); }});
    a.Add({"edit.redoAlt", "", "", CtrlShift(ImGuiKey_Z), {}, [&app] { return CanRedo(app); }, [&app] { Redo(app); }});
    a.Add({"edit.delete", N_("Удалить"), "ui-trash", ImGuiKey_Delete, {}, hasDoc, [&app] { DeletePressed(app); }});
    a.Add({"edit.escape", "", "", ImGuiKey_Escape, {}, {}, [&app] { EscapePressed(app); }});
    a.Add({"edit.confirm", "", "", ImGuiKey_Enter, {}, {}, [&app] { EnterPressed(app); }});
    a.Add({"edit.confirmPad", "", "", ImGuiKey_KeypadEnter, {}, {}, [&app] { EnterPressed(app); }});

    // --- model ---
    a.Add({"sketch.start", N_("Начать эскиз"), "sketch", ImGuiKey_S,
           N_("Выберите плоскость или плоскую грань и рисуйте профиль"), inModel, [&app] { StartSketch(app); }});
    a.Add({"feature.extrude", N_("Выдавливание"), "extrude", ImGuiKey_E,
           N_("Вытянуть замкнутый контур эскиза в тело (E)"), hasDoc, [&app] { StartExtrude(app); }});
    a.Add({"feature.revolve", N_("Вращение"), "revolve", ImGuiKey_R,
           N_("Повернуть замкнутый контур эскиза вокруг оси (R)"), inModel, [&app] { StartRevolve(app); }});
    a.Add({"feature.fillet", N_("Сопряжение"), "fillet", 0, N_("Скруглить рёбра тела"), inModel, [&app] { StartFillet(app); }});
    a.Add({"feature.hole", N_("Отверстие"), "hole", ImGuiKey_H, N_("Просверлить отверстия в плоской грани: простые, с цековкой или зенковкой (H)"),
           inModel, [&app] { StartHole(app); }});
    a.Add({"feature.thread", N_("Резьба"), "thread", 0, N_("Метрическая резьба на валу или в отверстии (ISO, крупный шаг)"),
           inModel, [&app] { StartThread(app); }});
    a.Add({"feature.workPlane", N_("Плоскость"), "work-plane", 0, N_("Рабочая плоскость: смещение, посередине, под углом, по трём точкам, касательная"), inModel,
           [&app] { StartWorkPlane(app); }});
    a.Add({"feature.workAxis", N_("Ось"), "work-axis", 0, N_("Рабочая ось: по оси цилиндра, по ребру или на пересечении плоскостей"), inModel,
           [&app] { StartWorkAxis(app); }});
    a.Add({"parts.library", N_("Библиотека"), "library", 0,
           N_("Стандартные изделия: болты, винты, гайки, шайбы, подшипники, стопорные кольца — новой деталью"), {},
           [&app] { app.shell.showLibrary = true; }});
    a.Add({"feature.workPoint", N_("Точка"), "work-point", 0,
           N_("Рабочая точка: вершина, центр окружности или сферы, середина ребра, пересечение с плоскостью, координаты"), inModel,
           [&app] { StartWorkPoint(app); }});
    a.Add({"feature.patternRect", N_("Прямоугольный"), "pattern-rect", 0, N_("Повторить операции рядами вдоль осей"), inModel,
           [&app] { StartPattern(app, doc::PatternKind::Rectangular); }});
    a.Add({"feature.patternCircular", N_("Круговой"), "pattern-circular", 0, N_("Повторить операции по кругу вокруг оси"), inModel,
           [&app] { StartPattern(app, doc::PatternKind::Circular); }});
    a.Add({"feature.mirror", N_("Зеркало"), "mirror", 0, N_("Отразить операции относительно плоскости"), inModel,
           [&app] { StartMirror(app); }});
    a.Add({"feature.sweep", N_("Сдвиг"), "sweep", 0, N_("Протянуть контур эскиза вдоль траектории из другого эскиза"), inModel,
           [&app] { StartSweep(app); }});
    a.Add({"feature.loft", N_("Лофт"), "loft", 0, N_("Тело через контуры нескольких эскизов"), inModel, [&app] { StartLoft(app); }});
    a.Add({"feature.coil", N_("Пружина"), "coil", 0, N_("Проволока, навитая по винтовой линии вокруг оси; вырезом — канавка"), inModel,
           [&app] { StartCoil(app); }});
    a.Add({"feature.section", N_("Сечение"), "cut-section", 0, N_("Отсечь модель плоскостью — операция в дереве, гасится и удаляется как любая"), inModel,
           [&app] { StartSection(app); }});
    a.Add({"feature.shell", N_("Оболочка"), "shell", 0, N_("Сделать тело полым со стенками заданной толщины; выбранные грани удаляются"),
           inModel, [&app] { StartShell(app); }});
    a.Add({"feature.chamfer", N_("Фаска"), "chamfer", 0, N_("Срезать рёбра тела под 45°"), inModel, [&app] { StartChamfer(app); }});

    // --- sketch ---
    a.Add({"sketch.finish", N_("Принять эскиз"), "sk-finish", 0, N_("Выйти из эскиза (Esc при пустом выборе)"), inSketch, [&app] {
               if (Session* s = app.Doc()) app.sketch.Finish(app, *s);
           }});
    a.Add({"sketch.lookAt", N_("Смотреть на эскиз"), "sk-look-at", ImGuiKey_F6, N_("Развернуть камеру к плоскости эскиза (F6)"),
           inSketch, [&app] {
               if (Session* s = app.Doc()) app.sketch.LookAtSketch(app, *s);
           }});
    a.Add({"sketch.construction", N_("Вспомогательная"), "sk-construction", ImGuiKey_G,
           N_("Сделать выбранное вспомогательной геометрией: она не образует контуров (G)"),
           [&app] { return app.Doc() && app.Doc()->mode == EditMode::Sketch && app.sketch.HasSelection(); },
           [&app] { app.sketch.ToggleConstruction(*app.Doc()); }});
    a.Add({"sketch.ortho", N_("Орто"), "c-perpendicular", ImGuiKey_F8,
           N_("Ортогональный режим: отрезки рисуются только горизонтально или вертикально (F8)"),
           [&app] { return app.Doc() && app.Doc()->mode == EditMode::Sketch; },
           [&app] { app.settings.orthoMode = !app.settings.orthoMode; }});
    a.Add({"sketch.centerline", N_("Осевая"), "sk-centerline", 0,
           N_("Сделать выбранные отрезки осевыми (штрихпунктир): ось для вращения и пружины, контур ограничивают как обычные"),
           [&app] { return app.Doc() && app.Doc()->mode == EditMode::Sketch && app.sketch.HasSelection(); },
           [&app] { app.sketch.ToggleCenterline(*app.Doc()); }});
    a.Add({"sketch.showConstraints", N_("Показать все зависимости"), "c-show-all", ImGuiKey_F8,
           N_("Значки всех зависимостей эскиза (F8). Скрытые видны у выделенного"), inSketch,
           [&app] { app.settings.showAllConstraints = true; }});
    a.Add({"sketch.hideConstraints", N_("Скрыть зависимости"), "c-hide-all", ImGuiKey_F9,
           N_("Показывать значки только у выделенного (F9)"), inSketch, [&app] { app.settings.showAllConstraints = false; }});
    const struct {
        SketchTool  tool;
        const char* id;
        ImGuiKey    key;
    } tools[] = {
        {SketchTool::Line, "sketch.line", ImGuiKey_L},          {SketchTool::Rectangle, "sketch.rectangle", ImGuiKey_R},
        {SketchTool::Circle, "sketch.circle", ImGuiKey_C},      {SketchTool::Arc, "sketch.arc", ImGuiKey_A},
        {SketchTool::Point, "sketch.point", ImGuiKey_P},        {SketchTool::Dimension, "sketch.dimension", ImGuiKey_D},
        {SketchTool::Project, "sketch.project", ImGuiKey_None}, {SketchTool::Trim, "sketch.trim", ImGuiKey_T},
        {SketchTool::Extend, "sketch.extend", ImGuiKey_None},
        {SketchTool::Split, "sketch.split", ImGuiKey_None},
        {SketchTool::Fillet, "sketch.fillet", ImGuiKey_None},
        {SketchTool::Offset, "sketch.offset", ImGuiKey_O},
        {SketchTool::Spline, "sketch.spline", ImGuiKey_None},
        {SketchTool::Text, "sketch.text", ImGuiKey_None},
        {SketchTool::Move, "sketch.move", ImGuiKey_M},          {SketchTool::Rotate, "sketch.rotate", ImGuiKey_None},
        {SketchTool::Coincident, "sketch.coincident", ImGuiKey_None},
        {SketchTool::Horizontal, "sketch.horizontal", ImGuiKey_None},
        {SketchTool::Vertical, "sketch.vertical", ImGuiKey_None},
        {SketchTool::Parallel, "sketch.parallel", ImGuiKey_None},
        {SketchTool::Perpendicular, "sketch.perpendicular", ImGuiKey_None},
        {SketchTool::Equal, "sketch.equal", ImGuiKey_None},     {SketchTool::Tangent, "sketch.tangent", ImGuiKey_None},
        {SketchTool::Fix, "sketch.fix", ImGuiKey_None},         {SketchTool::Midpoint, "sketch.midpoint", ImGuiKey_None},
        {SketchTool::Concentric, "sketch.concentric", ImGuiKey_None},
        {SketchTool::Symmetric, "sketch.symmetric", ImGuiKey_None},
        {SketchTool::Mirror, "sketch.mirror", ImGuiKey_None},
        {SketchTool::Copy, "sketch.copy", ImGuiKey_None},
        {SketchTool::Scale, "sketch.scale", ImGuiKey_None},
        {SketchTool::PatternRect, "sketch.patternRect", ImGuiKey_None},
        {SketchTool::PatternCircular, "sketch.patternCircular", ImGuiKey_None},
    };
    for (const auto& t : tools) {
        const SketchToolInfo& info = InfoOf(t.tool);
        const SketchTool tool = t.tool;
        a.Add({t.id, info.label, info.icon, t.key == ImGuiKey_None ? 0 : static_cast<ImGuiKeyChord>(t.key), info.hint,
               inSketch, [&app, tool] {
                   if (tool == SketchTool::Project) app.sketch.SetProjectLinked(true);
                   app.sketch.SetTool(app, *app.Doc(), tool);
               }});
    }
    // The same tool, making a copy that no longer follows the model.
    a.Add({"sketch.projectFixed", N_("Независимая проекция"), "sk-project-fixed", 0,
           N_("Щёлкните ребро модели или грань — её контур станет обычной закреплённой геометрией эскиза, за моделью не следует"),
           inSketch, [&app] {
               app.sketch.SetProjectLinked(false);
               app.sketch.SetTool(app, *app.Doc(), SketchTool::Project);
           }});

    // --- view ---
    a.Add({"view.home", N_("Домой (изометрия)"), "view-home", ImGuiKey_Home, {}, hasDoc, [&app] { HomeView(app); }});
    a.Add({"view.fit", N_("Показать всё"), "view-fit", ImGuiKey_F, {}, hasDoc, [&app] { FitView(app); }});
    a.Add({"view.front", N_("Вид спереди"), "tree-plane", 0, {}, hasDoc, [&app] { LookFrom(app, {0, -1, 0}, {0, 0, 1}); }});
    a.Add({"view.top", N_("Вид сверху"), "tree-plane", 0, {}, hasDoc, [&app] { LookFrom(app, {0, 0, 1}, {0, 1, 0}); }});
    a.Add({"view.right", N_("Вид справа"), "tree-plane", 0, {}, hasDoc, [&app] { LookFrom(app, {1, 0, 0}, {0, 0, 1}); }});
    const struct {
        render::DisplayStyle style;
        const char*          id;
        const char*          label;
        const char*          icon;
    } styles[] = {
        {render::DisplayStyle::Shaded, "view.shaded", N_("Тонированный"), "ds-shaded"},
        {render::DisplayStyle::ShadedWithEdges, "view.edges", N_("С рёбрами"), "ds-shaded-edges"},
        {render::DisplayStyle::Wireframe, "view.wireframe", N_("Каркас"), "ds-wireframe"},
        {render::DisplayStyle::HiddenLines, "view.hidden", N_("Скрытые линии"), "ds-hidden-lines"},
        {render::DisplayStyle::Transparent, "view.transparent", N_("Прозрачный"), "ds-transparent"},
    };
    for (const auto& s : styles) {
        const render::DisplayStyle style = s.style;
        a.Add({s.id, s.label, s.icon, 0, N_("Стиль отображения"), {}, [&app, style] { app.shell.displayStyle = style; }});
    }
    a.Add({"view.themeLight", N_("Светлая тема"), "ui-sun", 0, {}, {}, [&app] { SetTheme(app, ThemeChoice::Light); }});
    a.Add({"view.themeDark", N_("Тёмная тема"), "ui-moon", 0, {}, {}, [&app] { SetTheme(app, ThemeChoice::Dark); }});
    a.Add({"view.themeSystem", N_("Тема как в системе"), "theme", 0, {}, {}, [&app] { SetTheme(app, ThemeChoice::System); }});
    a.Add({"view.animations", N_("Анимации камеры"), "", 0, N_("Плавные повороты камеры"), {},
           [&app] { app.settings.animations = !app.settings.animations; }});

    // --- left panel ---
    a.Add({"panel.tree", N_("Дерево построения"), "ui-tree", 0, {}, {}, [&app] { app.shell.leftTab = LeftTab::Tree; }});
    a.Add({"panel.properties", N_("Свойства детали"), "ui-sliders", 0, {}, {}, [&app] { app.shell.leftTab = LeftTab::Properties; }});
    a.Add({"panel.variables", N_("Переменные"), "ui-variables", 0, {}, {}, [&app] { app.shell.leftTab = LeftTab::Variables; }});
}

} // namespace bz::app
