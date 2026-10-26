# BreezeCAD — архитектура (для программистов)

C++20, Dear ImGui (интерфейс, immediate mode), OCCT (геометрическое ядро),
GLFW + OpenGL 3.3. Сборка — CMake presets (`linux-debug`, Windows — MSYS2/MinGW64
в CI). Тесты — doctest (`build/<preset>/bin/bz_tests`).

## Модули и зависимости

```
core      ← математика, единицы, выражения, лог, перевод (I18n)
sketch    ← core            эскиз 2D: сущности, связи, решатель, операции правки
sheetmetal ← core           листовой металл без ядра: правила гиба, раскладка отбортовок, развёртка,
                            DXF (чтение любого, запись R12 для AutoCAD), проверки для резки (ADR-0007)
document  ← core, sketch, sheetmetal   дерево операций, параметры, команды undo, файл .bzc
kernel    ← document, OCCT  перестроение модели (тела, регионы эскизов, экспорт)
modules   ← kernel          модули: module.json (требования к железу), загрузка/отключение,
                            защита от падений, C-ABI `modules/bz_module.h`, пример `examples/modules/hello`;
                            команды для модулей — `app/scripting` (JSON, ADR-0002); модули ставятся пакетами `.bzmod` (`ModuleManager::Install`)
render    ← core, kernel    вьюпорт OCCT/OpenGL, выбор (pick), подсветка
ui        ← core, ImGui     тема, шрифты, иконки, виджеты, видовой куб
platform  ← core            окна, пути, файловые диалоги, запуск браузера/папки, жесты тачпада
                            (Touchpad: Cocoa / Win32 / XInput 2 — одна модель событий)
app       ← всё выше        оболочка: лента, панели, редакторы операций и эскиза
```
Правило: стрелки только вниз. `sketch` и `document` не знают про OCCT и ImGui —
их можно тестировать без окна; `kernel` не знает про интерфейс.

## Поток данных

1. **Документ** (`doc::Document`): упорядоченный список `Feature` (эскизы, операции,
   рабочие плоскости) + параметры (`Parameter`: имя, выражение, значение).
   Ссылки между операциями — по `ObjectId`; ссылки на геометрию тела —
   по описанию (`FaceReference`, `EdgeReference`), а не по индексам OCCT:
   после перестроения они находятся заново (`kernel/occt/FaceMatch`, `EdgeMatch`).
2. **Изменение** идёт командой (`doc::Command`: Do/Undo/Label) через
   `Session::Execute`, или правкой на месте с копией «до»
   (`Session::CommitInPlaceEdit(before, label)`) — так устроены эскиз и панели операций.
   Изменённая операция помечается `Document::MarkChanged(index)`.
3. **Перестроение** (`kernel::Model::Rebuild(doc)`): с первой изменённой операции
   до конца; у каждой операции `status` (Ok / Warning / Error + текст по-русски).
   Регионы эскизов (замкнутые контуры) строит OCCT из рёбер эскиза.
4. **Показ**: `render::Viewport::Sync(model)` пересоздаёт тела, когда сменилась ревизия.
   Рисуется по требованию (кадр — только когда что-то изменилось).

## Эскиз

- `sketch::Sketch`: Point / Line / Circle / Arc / Spline + Constraint
  (геометрические и размерные, до 3 ссылок). Кривые ссылаются на точки по id.
- Решатель: `sketch::Solve` (демпфированный Гаусс–Ньютон минимальной нормы по `EquationSystem`), анализ степеней
  свободы и «полностью определённых» элементов.
- Операции правки — свободные функции в `src/sketch/*` (Shapes, Trim/Extend/Split,
  Fillet, Offset, Move, Mirror, SplineGeometry) с тестами в `tests/sketch`.
- Редактор (`app/sketch`): `SketchEditor` (состояние, диспетчер инструментов),
  `SketchTools.cpp` (Handle* для каждого инструмента), `SketchToolPanel.cpp`
  (левая панель), `SketchOverlay`/`SketchPick` (рисование/выбор), `DimensionLayout`.

## Интерфейс

- Оболочка (`app/shell`): Header (меню, панель быстрого доступа), Ribbon (лента),
  LeftPanels/TreePanel (дерево, свойства, переменные), Bars (строка состояния), Dialogs.
- Команды — `ActionRegistry` (`AppActions.cpp`): id, подпись, иконка, сочетание,
  условие доступности. Лента, меню, поиск команд и клавиши берут их отсюда.
- Панели операций — `OperationEditor` → `FeatureOperationEditor` →
  `SketchOperationEditor` (операции от эскиза). Живой предпросмотр: операция уже
  в документе, Отмена восстанавливает. Манипуляторы — `features/DragArrow`.
- Перевод: весь видимый текст через `Tr("…")`/`Trf("…{}", x)`; ключ — русский текст;
  каталоги `assets/lang/*.json` или папка пользователя (см. `translations/README.md`).
- Настройки: `app/Settings` (JSON в папке пользователя), окно — `Dialogs.cpp`.

## Файл .bzc
Zip: `manifest.json` (версия формата) + `document.json` (дерево, параметры, эскизы). Неизвестные типы
операций сохраняются и показываются с ошибкой, а не теряются; новые поля читаются
`ReadMember` (отсутствует — значение по умолчанию), поэтому старые файлы открываются.

## Команды для модулей (ADR-0002)
Модуль меняет документ только командами: `host->execute("{\"command\": …}")` в главном
потоке (из своего потока — `request_main()` → `main_tick()`). Команды — `app/scripting/`:
таблица `Command` (имя, описание, JSON-схема, `changes`, функция), группы по файлам
(`DocumentCommands`, `PartCommands`, `SketchCommands`, `SolidCommands`, `TopologyCommands` — грани и
рёбра по номерам из `kernel/Topology.h`, `GenericCommands` — любая операция по полям, `FeatureCommands`,
`WorkCommands`, `ViewCommands` — картинка `Viewport::Snapshot`, внешний вид, библиотека,
`AssemblyCommands`). Правка — через `Session::Execute` (сборка — `AssemblyState::Checkpoint`), один
вызов = один шаг отмены; операция, которая не построилась, откатывается (`RollBackIfFailed`). Модули сами по себе —
пакеты `.bzmod` (ZIP: папка модуля под одну систему), их ставит `ModuleManager::Install` в папку модулей
настроек («Настройки → Модули» или перетаскиванием в окно), включает сразу, удаляет `Uninstall`. MCP-модуль
для Claude — такой пакет (отдельный PR, `modules/mcp`).

## Листовой металл (ADR-0007)
Операции `sheetMetal` («Лист»), `sheetFlange` («Отбортовка»), `sheetCut` («Вырез») хранят только ввод.
Модель листа (`sheet::Part`: основание 2D, отбортовки, вырезы, правила) живёт в `src/sheetmetal` без
OCCT; `sheet::Solve` раскладывает отбортовки и считает положение каждого сгиба в координатах детали,
`sheet::Flatten` — развёртку. Ядро (`kernel/occt/SheetMetal.cpp`) только строит тело из раскладки:
призмы, секторы вращения, объединение, вырезы, проверка столкновений полок. Состояние листа после
каждой листовой операции — `Model::SheetAt(id)` / `Model::Sheet(doc, base)`; окно «Развёртка» и
«Редактор DXF» (`app/features/DxfEditor`) берут его оттуда. Команды: `sheet.*`, `dxf.*`
(`app/scripting/SheetCommands.cpp`).

## Как добавить…
- **Инструмент эскиза**: `SketchTool` (SketchEditor.h) → строка в `kTools`
  (SketchEditor.cpp, размер массива +1) → `case` в диспетчере → `Handle…` в
  SketchTools.cpp → строка в `tools[]` (AppActions.cpp) → `r.Small(...)` (Ribbon.cpp)
  → при нужде поле в SketchToolPanel.cpp → иконка `assets/icons/sk-*.svg`.
  Геометрию — отдельной функцией в `src/sketch` с тестом.
- **Операцию модели**: `document/XxxFeature.{h,cpp}` (поля, Save/Load, регистрация в
  FeatureRegistry) → применение в `kernel/Model.cpp` (+ `occt/Xxx.cpp`) → панель
  `app/features/XxxEditor` → команда в AppActions + кнопка в Ribbon → тест в `tests/kernel`.
- **Команду для модулей/MCP**: функция `Json Xxx(AppContext&, const Json& args)` в своей
  группе `app/scripting/*Commands.cpp` + строка в её `AddXxxCommands` (описание для ИИ, схема
  через `Schema`/`NumberArg`/…, `changes = true`, если меняет документ). Модуль не трогать.
- **Настройку**: поле в `Settings.h`, чтение/запись в `Settings.cpp`, строка в
  `Dialogs.cpp`.

## Проверки
`bz_tests` (ядро эскиза, документ, ядро OCCT), UI-сценарии `scripts/ui-test/*.sh`
на Xvfb (Linux), CI: Windows, Linux, macOS (Apple Silicon) — сборка, тесты и по два пакета
(обычный и для тестеров, `scripts/package-*.sh`); все шесть уходят в выпуск `next-build`.
