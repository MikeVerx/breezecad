// Every user command, registered once. Ribbon, menus, the command search and
// keyboard shortcuts all run actions from here.

#include "app/Application.h"
#include "app/Operations.h"
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
    a.Add({"file.new", "Новая деталь", "ui-new", Ctrl(ImGuiKey_N), {}, {}, [&app] { NewDocument(app); }});
    a.Add({"file.open", "Открыть…", "ui-open", Ctrl(ImGuiKey_O), {}, {}, [&app] { OpenDocument(app); }});
    a.Add({"file.save", "Сохранить", "ui-save", Ctrl(ImGuiKey_S), {}, hasDoc, [&app] { SaveDocument(app, false); }});
    a.Add({"file.saveAs", "Сохранить как…", "ui-save", CtrlShift(ImGuiKey_S), {}, hasDoc, [&app] { SaveDocument(app, true); }});
    a.Add({"file.close", "Закрыть документ", "ui-close", Ctrl(ImGuiKey_W), {}, hasDoc,
           [&app] { RequestClose(app, app.workspace.ActiveIndex()); }});
    a.Add({"file.exportStep", "Экспорт в STEP", "export", 0, "Для обмена с другими САПР (AP214)", hasDoc,
           [&app] { ExportDocument(app, ExportFormat::Step); }});
    a.Add({"file.exportStl", "Экспорт в STL", "export", 0, "Сетка для 3D-печати", hasDoc,
           [&app] { ExportDocument(app, ExportFormat::Stl); }});
    a.Add({"file.export3mf", "Экспорт в 3MF", "export", 0, "Современный формат для слайсеров", hasDoc,
           [&app] { ExportDocument(app, ExportFormat::ThreeMf); }});
    a.Add({"app.quit", "Выход", "", Ctrl(ImGuiKey_Q), {}, {}, [&app] { app.shell.quitRequested = true; }});
    a.Add({"app.home", "Главная страница", "ui-home", 0, {}, {}, [&app] { app.workspace.ShowHome(true); }});
    a.Add({"app.search", "Поиск команд", "ui-search", Ctrl(ImGuiKey_K), {}, {}, [&app] {
               app.shell.paletteOpen = true;
               app.shell.focusPalette = true;
           }});
    a.Add({"app.about", "О программе", "ui-info", 0, {}, {}, [&app] { app.shell.showAbout = true; }});

    // --- edit ---
    a.Add({"edit.undo", "Отменить", "ui-undo", Ctrl(ImGuiKey_Z), {}, [&app] { return CanUndo(app); }, [&app] { Undo(app); }});
    a.Add({"edit.redo", "Повторить", "ui-redo", Ctrl(ImGuiKey_Y), {}, [&app] { return CanRedo(app); }, [&app] { Redo(app); }});
    a.Add({"edit.redoAlt", "", "", CtrlShift(ImGuiKey_Z), {}, [&app] { return CanRedo(app); }, [&app] { Redo(app); }});
    a.Add({"edit.delete", "Удалить", "ui-trash", ImGuiKey_Delete, {}, hasDoc, [&app] { DeletePressed(app); }});
    a.Add({"edit.escape", "", "", ImGuiKey_Escape, {}, {}, [&app] { EscapePressed(app); }});
    a.Add({"edit.confirm", "", "", ImGuiKey_Enter, {}, {}, [&app] { EnterPressed(app); }});
    a.Add({"edit.confirmPad", "", "", ImGuiKey_KeypadEnter, {}, {}, [&app] { EnterPressed(app); }});

    // --- model ---
    a.Add({"sketch.start", "Начать эскиз", "sketch", ImGuiKey_S,
           "Выберите плоскость или плоскую грань и рисуйте профиль", inModel, [&app] { StartSketch(app); }});
    a.Add({"feature.extrude", "Выдавливание", "extrude", ImGuiKey_E,
           "Вытянуть замкнутый контур эскиза в тело (E)", hasDoc, [&app] { StartExtrude(app); }});
    a.Add({"feature.revolve", "Вращение", "revolve", ImGuiKey_R,
           "Повернуть замкнутый контур эскиза вокруг оси (R)", inModel, [&app] { StartRevolve(app); }});

    // --- sketch ---
    a.Add({"sketch.finish", "Принять эскиз", "sk-finish", 0, "Выйти из эскиза (Esc при пустом выборе)", inSketch, [&app] {
               if (Session* s = app.Doc()) app.sketch.Finish(app, *s);
           }});
    a.Add({"sketch.lookAt", "Смотреть на эскиз", "sk-look-at", ImGuiKey_F6, "Развернуть камеру к плоскости эскиза (F6)",
           inSketch, [&app] {
               if (Session* s = app.Doc()) app.sketch.LookAtSketch(app, *s);
           }});
    a.Add({"sketch.construction", "Вспомогательная", "sk-construction", ImGuiKey_G,
           "Сделать выбранное вспомогательной геометрией: она не образует контуров (G)",
           [&app] { return app.Doc() && app.Doc()->mode == EditMode::Sketch && app.sketch.HasSelection(); },
           [&app] { app.sketch.ToggleConstruction(*app.Doc()); }});
    const struct {
        SketchTool  tool;
        const char* id;
        ImGuiKey    key;
    } tools[] = {
        {SketchTool::Line, "sketch.line", ImGuiKey_L},          {SketchTool::Rectangle, "sketch.rectangle", ImGuiKey_R},
        {SketchTool::Circle, "sketch.circle", ImGuiKey_C},      {SketchTool::Arc, "sketch.arc", ImGuiKey_A},
        {SketchTool::Point, "sketch.point", ImGuiKey_P},        {SketchTool::Dimension, "sketch.dimension", ImGuiKey_D},
        {SketchTool::Coincident, "sketch.coincident", ImGuiKey_None},
        {SketchTool::Horizontal, "sketch.horizontal", ImGuiKey_None},
        {SketchTool::Vertical, "sketch.vertical", ImGuiKey_None},
        {SketchTool::Parallel, "sketch.parallel", ImGuiKey_None},
        {SketchTool::Perpendicular, "sketch.perpendicular", ImGuiKey_None},
        {SketchTool::Equal, "sketch.equal", ImGuiKey_None},     {SketchTool::Tangent, "sketch.tangent", ImGuiKey_None},
        {SketchTool::Fix, "sketch.fix", ImGuiKey_None},         {SketchTool::Midpoint, "sketch.midpoint", ImGuiKey_None},
        {SketchTool::Concentric, "sketch.concentric", ImGuiKey_None},
    };
    for (const auto& t : tools) {
        const SketchToolInfo& info = InfoOf(t.tool);
        const SketchTool tool = t.tool;
        a.Add({t.id, info.label, info.icon, t.key == ImGuiKey_None ? 0 : static_cast<ImGuiKeyChord>(t.key), info.hint,
               inSketch, [&app, tool] { app.sketch.SetTool(app, *app.Doc(), tool); }});
    }

    // --- view ---
    a.Add({"view.home", "Домой (изометрия)", "view-home", ImGuiKey_Home, {}, hasDoc, [&app] { HomeView(app); }});
    a.Add({"view.fit", "Показать всё", "view-fit", ImGuiKey_F, {}, hasDoc, [&app] { FitView(app); }});
    a.Add({"view.front", "Вид спереди", "tree-plane", 0, {}, hasDoc, [&app] { LookFrom(app, {0, -1, 0}, {0, 0, 1}); }});
    a.Add({"view.top", "Вид сверху", "tree-plane", 0, {}, hasDoc, [&app] { LookFrom(app, {0, 0, 1}, {0, 1, 0}); }});
    a.Add({"view.right", "Вид справа", "tree-plane", 0, {}, hasDoc, [&app] { LookFrom(app, {1, 0, 0}, {0, 0, 1}); }});
    const struct {
        render::DisplayStyle style;
        const char*          id;
        const char*          label;
        const char*          icon;
    } styles[] = {
        {render::DisplayStyle::Shaded, "view.shaded", "Тонированный", "ds-shaded"},
        {render::DisplayStyle::ShadedWithEdges, "view.edges", "С рёбрами", "ds-shaded-edges"},
        {render::DisplayStyle::Wireframe, "view.wireframe", "Каркас", "ds-wireframe"},
        {render::DisplayStyle::HiddenLines, "view.hidden", "Скрытые линии", "ds-hidden-lines"},
        {render::DisplayStyle::Transparent, "view.transparent", "Прозрачный", "ds-transparent"},
    };
    for (const auto& s : styles) {
        const render::DisplayStyle style = s.style;
        a.Add({s.id, s.label, s.icon, 0, "Стиль отображения", {}, [&app, style] { app.shell.displayStyle = style; }});
    }
    a.Add({"view.themeLight", "Светлая тема", "ui-sun", 0, {}, {}, [&app] { SetTheme(app, ThemeChoice::Light); }});
    a.Add({"view.themeDark", "Тёмная тема", "ui-moon", 0, {}, {}, [&app] { SetTheme(app, ThemeChoice::Dark); }});
    a.Add({"view.themeSystem", "Тема как в системе", "theme", 0, {}, {}, [&app] { SetTheme(app, ThemeChoice::System); }});
    a.Add({"view.animations", "Анимации камеры", "", 0, "Плавные повороты камеры", {},
           [&app] { app.settings.animations = !app.settings.animations; }});

    // --- left panel ---
    a.Add({"panel.tree", "Дерево построения", "ui-tree", 0, {}, {}, [&app] { app.shell.leftTab = LeftTab::Tree; }});
    a.Add({"panel.properties", "Свойства детали", "ui-sliders", 0, {}, {}, [&app] { app.shell.leftTab = LeftTab::Properties; }});
    a.Add({"panel.variables", "Переменные", "ui-variables", 0, {}, {}, [&app] { app.shell.leftTab = LeftTab::Variables; }});
}

} // namespace bz::app
