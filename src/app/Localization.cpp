#include "app/Localization.h"
#include <unordered_map>

namespace BreezeCAD {

namespace {

const std::unordered_map<std::string, std::pair<std::string, std::string>> kDict = {
    // Общие
    { "btn.ok",              { "OK",                    "ОК" } },
    { "btn.cancel",          { "Cancel",                "Отмена" } },
    { "btn.delete",          { "Delete",                "Удалить" } },
    { "btn.apply",           { "Apply",                 "Применить" } },
    { "btn.createSketch",    { "Create Sketch",         "Создать эскиз" } },

    // QAT
    { "qat.new",             { "New",                   "Создать" } },
    { "qat.open",            { "Open",                  "Открыть" } },
    { "qat.save",            { "Save",                  "Сохранить" } },
    { "qat.undo",            { "Undo",                  "Отменить" } },
    { "qat.redo",            { "Redo",                  "Повторить" } },

    // Вкладки
    { "tab.sketch",          { "Sketch",                "Эскиз" } },
    { "tab.model",           { "3D Model",              "3D Модель" } },
    { "tab.annotate",        { "Annotate",              "Оформление" } },
    { "tab.tools",           { "Tools",                 "Инструменты" } },
    { "tab.manage",          { "Manage",                "Управление" } },
    { "tab.view",            { "View",                  "Вид" } },

    // Группы
    { "grp.create",          { "Create",                "Создать" } },
    { "grp.modify",          { "Modify",                "Изменить" } },
    { "grp.constrain",       { "Constrain",             "Ограничения" } },
    { "grp.exit",            { "Exit",                  "Выход" } },
    { "grp.work",            { "Work Features",         "Рабочие элементы" } },

    // Инструменты — Линия
    { "tool.line",           { "Line",                  "Отрезок" } },
    { "tool.curve",          { "Curve",                 "Кривая" } },
    { "tool.spline",         { "Spline by points",      "Сплайн по точкам" } },
    { "tool.bridge",         { "Bridge curve",          "Соединительная кривая" } },
    { "tool.more",           { "More options",            "Дополнительные опции" } },

    // Прямоугольник
    { "tool.rect.2pt",       { "Two Point Rectangle",   "Прямоугольник по двум точкам" } },
    { "tool.rect.center",    { "Center Rectangle",      "Прямоугольник по центру" } },
    { "tool.rect.3pt",       { "Three Point Rectangle", "Прямоугольник по трём точкам" } },

    // Окружность
    { "tool.circle.center",  { "Center Point Circle",   "Окружность по центру" } },
    { "tool.circle.2pt",     { "Two Point Circle",      "Окружность по двум точкам" } },
    { "tool.circle.3pt",     { "Three Point Circle",    "Окружность по трём точкам" } },
    { "tool.circle.tan",     { "Tangent Circle",        "Касательная окружность" } },

    // Дуга
    { "tool.arc.center",     { "Center Point Arc",      "Дуга по центру" } },
    { "tool.arc.3pt",        { "Three Point Arc",       "Дуга по трём точкам" } },
    { "tool.arc.tan",        { "Tangent Arc",           "Касательная дуга" } },

    // Многоугольник
    { "tool.poly.inscribed", { "Inscribed Polygon",     "Вписанный многоугольник" } },
    { "tool.poly.circum",    { "Circumscribed Polygon", "Описанный многоугольник" } },

    // Modify
    { "tool.trim",           { "Trim",                  "Обрезать" } },
    { "tool.extend",         { "Extend",                "Удлинить" } },
    { "tool.offset",         { "Offset",                "Смещение" } },
    { "tool.move",           { "Move",                  "Переместить" } },
    { "tool.copy",           { "Copy",                  "Копировать" } },
    { "tool.rotate",         { "Rotate",                "Повернуть" } },
    { "tool.scale",          { "Scale",                 "Масштаб" } },
    { "tool.mirror",         { "Mirror",                "Зеркало" } },

    // Constrain
    { "tool.coincident",     { "Coincident",            "Совпадение" } },
    { "tool.parallel",       { "Parallel",              "Параллельность" } },
    { "tool.perpendicular",  { "Perpendicular",         "Перпендикулярность" } },
    { "tool.horizontal",     { "Horizontal",            "Горизонталь" } },
    { "tool.vertical",       { "Vertical",              "Вертикаль" } },
    { "tool.dimension",      { "Dimension",             "Размер" } },
    { "tool.tangent",        { "Tangent",               "Касание" } },
    { "tool.concentric",     { "Concentric",            "Концентричность" } },
    { "tool.equal",          { "Equal",                 "Равенство" } },

    // Exit
    { "tool.finish",         { "Finish Sketch",         "Завершить эскиз" } },

    // 3D Model
    { "tool.extrude",        { "Extrude",               "Выдавливание" } },
    { "tool.revolve",        { "Revolve",               "Вращение" } },
    { "tool.sweep",          { "Sweep",                 "Сдвиг" } },
    { "tool.loft",           { "Loft",                  "Лофт" } },
    { "tool.hole",           { "Hole",                  "Отверстие" } },
    { "tool.plane",          { "Plane",                 "Плоскость" } },
    { "tool.axis",           { "Axis",                  "Ось" } },
    { "tool.point",          { "Point",                 "Точка" } },
    { "tool.fillet",         { "Fillet",                "Скругление" } },
    { "tool.chamfer",        { "Chamfer",               "Фаска" } },
    { "tool.shell",          { "Shell",                 "Оболочка" } },
    { "tool.pattern",        { "Pattern",               "Массив" } },

    // Панель дерева
    { "panel.title",           { "Document Tree",          "Дерево документа" } },
    { "panel.hint.delete",     { "RMB on node - delete, Delete key - remove selected",
                                 "ПКМ по узлу — удалить, Delete — удалить выбранное" } },
    { "panel.document",        { "Document",               "Документ" } },
    { "panel.document.null",   { "Document: NULL",         "Документ: NULL" } },
    { "panel.addBox",          { "Add Box",                "Добавить куб" } },

    // Диалоги
    { "dlg.addBox.title",      { "Add Box",                "Добавить куб" } },
    { "dlg.name",              { "Name",                   "Имя" } },
    { "dlg.position",          { "Position (corner):",     "Позиция (угол):" } },
    { "dlg.size",              { "Size:",                  "Размеры:" } },

    // Debug
    { "debug.title",           { "BreezeCAD Debug",          "BreezeCAD — отладка" } },
    { "debug.controls",        { "Controls:",              "Управление:" } },
    { "debug.lmb",             { "LMB - select",           "ЛКМ — выбрать" } },
    { "debug.rmb",             { "RMB - rotate",           "ПКМ — вращение" } },
    { "debug.mmb",             { "MMB - pan",              "СКМ — панорама" } },
    { "debug.wheel",           { "Wheel - zoom",           "Колесо — зум" } },
    { "debug.delete",          { "Delete - remove selected", "Delete — удалить выбранное" } },
    { "debug.f11",             { "F11 - fullscreen",       "F11 — полный экран" } },
    { "debug.view.active",     { "OCCT view: active",      "3D-вид: активен" } },
    { "debug.view.inactive",   { "OCCT view: NOT initialized", "3D-вид: не инициализирован" } },
    { "debug.selected.tag",    { "Selected: tag=%d",       "Выбрано: тег=%d" } },
    { "debug.selected.none",   { "Selected: none",         "Выбрано: ничего" } },

    // Язык
    { "lang.label",            { "Language:",              "Язык:" } },
    { "lang.en",               { "English",                "English" } },
    { "lang.ru",               { "Русский",                "Русский" } },

    // Значения по умолчанию
    { "default.boxName",       { "Box",                    "Куб" } },
    { "default.cylinderName",  { "Cylinder",               "Цилиндр" } },
    { "default.holeName",      { "Hole",                   "Отверстие" } },

    // Эскиз — overlay
    { "sketch.mode",           { "SKETCH MODE",            "РЕЖИМ ЭСКИЗА" } },
    { "sketch.finish",         { "Finish Sketch",          "Завершить эскиз" } },
    { "sketch.clear",          { "Clear Sketch",           "Очистить эскиз" } },
    { "sketch.showGrid",       { "Show Grid",              "Показать сетку" } },
    { "sketch.snapping",       { "Snapping:",              "Привязки:" } },
    { "sketch.snapGrid",       { "To Grid (10mm)",         "К сетке (10мм)" } },
    { "sketch.snapPoints",     { "To Points",              "К точкам" } },
    { "sketch.autoContinue",   { "Auto-Continue",          "Автопродолжение" } },
    { "sketch.lineStyle",      { "Line Style:",            "Стиль линии:" } },
    { "sketch.normal",         { "Normal",                 "Основная" } },
    { "sketch.construction",   { "Construction",           "Вспомогательная" } },
    { "sketch.centerline",     { "Centerline",             "Осевая" } },
    { "sketch.hint.lmb",       { "LMB: click start, click end", "ЛКМ: клик начало, клик конец" } },
    { "sketch.hint.shift",     { "Shift: ortho (0/90 deg)", "Shift: орто (0/90°)" } },
    { "sketch.hint.esc",       { "Esc: cancel line / exit", "Esc: отмена линии / выход" } },
    { "sketch.exact",          { "Exact input:",           "Точный ввод:" } },
    { "sketch.length",         { "Length",                 "Длина" } },
    { "sketch.angle",          { "Angle",                  "Угол" } },
    { "grp.sketch",     { "Sketch",     "Эскиз" } },
    { "grp.project",    { "Project",    "Проецировать" } },
    { "grp.format",     { "Format",     "Формат" } },
    { "grp.pattern",    { "Pattern",    "Массив" } },
    { "grp.dimensions", { "Dimensions", "Размеры" } },
    { "grp.text",       { "Text & Symbols", "Текст и символы" } },
    { "grp.measure",    { "Measure",    "Измерить" } },
    { "grp.analysis",   { "Analysis",   "Анализ" } },
    { "grp.properties", { "Properties", "Свойства" } },
    { "grp.parameters", { "Parameters", "Параметры" } },
    { "grp.materials",  { "Materials",  "Материалы" } },
    { "grp.update",     { "Update",     "Обновление" } },
    { "grp.insert",     { "Insert",     "Вставка" } },
    { "grp.navigation", { "Navigation", "Навигация" } },
    { "grp.views",      { "Views",      "Виды" } },
    { "grp.display",    { "Display",    "Отображение" } },
    { "grp.visibility", { "Visibility", "Видимость" } },
    { "grp.windows",    { "Windows",    "Окна" } },
    { "tool.curve",     { "Curve",      "Кривая" } },
    { "tool.spline",    { "Spline",     "Сплайн" } },
    { "tool.bridge",    { "Bridge",     "Соединительная" } },
    { "tool.dummy",     { "-",          "-" } },
    { "tool.newSketch",      { "New Sketch",       "Создать эскиз" } },
    { "tool.union",          { "Union",            "Объединить" } },
    { "tool.cut",            { "Cut",              "Вычесть" } },
    { "tool.intersect",      { "Intersect",        "Пересечение" } },
    { "tool.thread",         { "Thread",           "Резьба" } },
    { "tool.chamfer",        { "Chamfer",          "Фаска" } },
    { "tool.draft_analysis", { "Draft Analysis",   "Анализ уклонов" } },
    { "tool.pattern_rect",   { "Rect Pattern",     "Прямоуг. массив" } },
    { "tool.pattern_circ",   { "Circ Pattern",     "Круговой массив" } },
    { "tool.dim_linear",     { "Linear Dim",       "Линейный размер" } },
    { "tool.dim_angle",      { "Angle Dim",        "Угловой размер" } },
    { "tool.dim_radius",     { "Radius Dim",       "Радиус" } },
    { "tool.dim_diameter",   { "Diameter Dim",     "Диаметр" } },
    { "tool.dim_ordinate",   { "Ordinate Dim",     "Ординатный" } },
    { "tool.leader",         { "Leader",           "Лидер" } },
    { "tool.datum",          { "Datum",            "База" } },
    { "tool.tolerance",      { "Tolerance",        "Допуск формы" } },
    { "tool.roughness",      { "Roughness",        "Шероховатость" } },
    { "tool.weld",           { "Weld",             "Сварной шов" } },
    { "tool.model_props",    { "Model Props",      "Свойства модели" } },
    { "tool.mass",           { "Mass",             "Масса" } },
    { "tool.parameters",     { "Parameters",       "Параметры" } },
    { "tool.functions",      { "Functions",        "Функции" } },
    { "tool.texts",          { "Texts",            "Тексты" } },
    { "tool.material",       { "Material",         "Материал" } },
    { "tool.texture",        { "Texture",          "Текстура" } },
    { "tool.appearance",     { "Appearance",       "Внешний вид" } },
    { "tool.update",         { "Update",           "Обновить" } },
    { "tool.recompute",      { "Recompute",        "Пересчитать" } },
    { "tool.report",         { "Report",           "Отчёт" } },
    { "tool.insert_obj",     { "Insert Object",    "Вставить объект" } },
    { "tool.insert_img",     { "Insert Image",     "Вставить изображение" } },
    { "tool.import",         { "Import",           "Импорт" } },
    { "tool.view_top",       { "Top",              "Сверху" } },
    { "tool.view_front",     { "Front",            "Спереди" } },
    { "tool.view_right",     { "Right",            "Справа" } },
    { "tool.view_iso",       { "Isometric",        "Изометрия" } },
    { "tool.shadows",        { "Shadows",          "Тени" } },
    { "tool.reflections",    { "Reflections",      "Отражения" } },
    { "tool.grid",           { "Grid",             "Сетка" } },
    { "tool.background",     { "Background",       "Фон" } },
    { "tool.hide",           { "Hide",             "Скрыть" } },
    { "tool.show_all",       { "Show All",         "Показать всё" } },
    { "tool.isolate",        { "Isolate",          "Изолировать" } },
    { "tool.show_hidden",    { "Show Hidden",      "Показать скрытое" } },
    { "tool.new_window",     { "New Window",       "Новое окно" } },
    { "tool.tile",           { "Tile",             "Мозаика" } },
    { "tool.cascade",        { "Cascade",          "Каскад" } },
    { "tool.pan",            { "Pan",              "Панорамировать" } },
    { "tool.zoom",           { "Zoom",             "Зум" } },
    { "tool.orbit",          { "Orbit",            "Орбита" } },
    { "tool.home",           { "Home View",        "Домой" } },
    { "tool.section",        { "Section",          "Сечение" } },
    { "tool.thickness",      { "Thickness",        "Толщина" } },
    { "tool.curvature",      { "Curvature",        "Кривизна" } },
    { "tool.zebra",          { "Zebra",            "Зебра" } },
    { "tool.interference",   { "Interference",     "Интерференция" } },
    { "tool.plane",          { "Work Plane",       "Рабочая плоскость" } },
    { "tool.axis",           { "Work Axis",        "Рабочая ось" } },
    { "tool.point",          { "Work Point",       "Рабочая точка" } },
    { "tool.shell",          { "Shell",            "Оболочка" } },
    { "tool.loft",           { "Loft",             "Лофт" } },
    { "tool.sweep",          { "Sweep",            "Сдвиг" } },
    { "tool.text",           { "Text",             "Текст" } },
    { "sketch.options",        { "Options",                "Параметры" } },
    { "sketch.only",           { "Sketch Only",            "Только в эскизе" } },
    { "panel.dependencies",    { "Dependencies",           "Зависимости" } },
    { "panel.deps.hint",       { "Select element to view constraints", "Выберите элемент для просмотра зависимостей" } },
    { "panel.deps.empty",      { "(no constraints yet)",   "(ограничений пока нет)" } },
    { "panel.deps.fully",      { "Fully Constrained",      "Полностью определено" } },
    { "panel.deps.dof",        { "DOF remaining",          "Осталось степеней свободы" } },

};

} // namespace

Localization& Localization::Instance() {
    static Localization instance;
    return instance;
}

std::string Localization::Tr(const std::string& key) const {
    auto it = kDict.find(key);
    if (it == kDict.end()) return key;
    return (myLang == Language::EN) ? it->second.first : it->second.second;
}

} // namespace BreezeCAD
