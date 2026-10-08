# BreezeCAD — MODULAR_REFACTOR_MAP

**Статус:** утверждён v3 (после ревью GPT)
**Создан:** 2026-10-08
**Источник:** анализ реального кода после Phase 1.5 + 8 правок ревью
**Цель:** CURRENT → TARGET с границами, зависимостями и уверенностью

**Не путать с** `MODULAR_REFACTOR.md` (манифест) — будет создан после закрытия Phase 1.6.

---

## 1. CURRENT COMPONENT MAP

### 1.1. Все модули `src/` с размерами

| Модуль | Строк | Файлов | Назначение |
|---|---|---|---|
| `app/` | **1817** | 4 | Application, IconManager, Localization, IconsFontAwesome |
| ├─ Application.cpp | 1567 | | оркестратор |
| ├─ Application.h | 234 | | 70+ полей состояния |
| ├─ IconManager.cpp | 260 | | загрузка SVG-иконок |
| ├─ Localization.cpp | 357 | | RU/EN словарь |
| └─ IconsFontAwesome.h | 170 | | макросы ICON_* |
| `occt/` | **1098** | 2 | OcctView (обёртка OCCT V3d) |
| `ui/viewcube/` | **632** | 3 | ViewCube (кубик навигации) |
| `ui/ribbon/` | **~800** | 12 | ribbon + 7 вкладок |
| `ui/home/` | **570** | 4 | HomeScreen, Thumbnails |
| `ui/panels/` | **947** | 12 | DocumentTree, ContextMenu, RightPanel |
| `ui/dialogs/` | **~800** | 16 | 8 диалогов |
| `ui/theme/` | **~490** | 8 | ThemeTokens, ThemePresets, ThemeManager, Fonts |
| `ui/widgets/` | **~330** | 8 | SvgButton, Tooltip, IconTint, SelectionRect |
| `ui/qat/` | **~306** | 2 | QuickAccessToolbar |
| `ui/status/` | **~257** | 4 | StatusBar, ProjectTabs |
| `doc/` | **~640** | 4 | BreezeApplication (OCAF), BreezeDocument |
| `platform/` | **~270** | 4 | FileDialog, GlfwWindow |
| `util/` | **~280** | 6 | Utf8, Geometry2D, LabelName |
| **Всего** | **10452** | 84 | |

### 1.2. Что живёт в `Application.cpp`

**1567 строк**, 31 функция, 13 смысловых групп (см. §5).

---

## 2. DEPENDENCY MAP (runtime)

### 2.1. Application → все

Application знает все UI-модули, документы, платформу. Это нормально для оркестратора.

### 2.2. Модули друг на друга (без Application)

| Модуль | Знает о | Проблема |
|---|---|---|
| `ui/ribbon/*` | RibbonState, IconManager, Tooltip | ✅ OK |
| `ui/panels/DocumentTree` | `doc/BreezeDocument` ✗, `occt/OcctView` ✗, 4 диалога ✗ | 🔴 Нарушение |
| `ui/viewcube/ViewCubeOverlay` | `app/Application.h` ✗✗✗, `occt/OcctView` ✗ | 🔴 Инверсия слоя |
| `ui/dialogs/*` | `doc/BreezeDocument` ✗ | 🟠 Средне |
| `ui/status/*` | IconManager, SvgButton | ✅ OK |
| `ui/home/*` | `occt/OcctView` ✗, **GL/gl.h** ✗✗✗ | 🔴 Грубо |
| `ui/qat/*` | IconManager, TooltipState, SvgButton | ✅ OK |
| `ui/theme/*` | OcctView (для градиента) | 🔴 Убрать |
| `ui/widgets/*` | IconManager | ✅ OK |

---

## 3. DEPENDENCY DIRECTION MAP

### 3.1. Целевое направление

Слои **не линейные**. `Platform` — сервисный слой, который могут использовать `Application` и rendering, но не через линейную цепочку.

```
                  Application
                  /    |    \
                 /     |     \
               UI   Commands  Platform
                \      |
                 \     |
                 Document
                     |
                Geometry API
                   /     \
                  /       \
              OCCT        Other Kernel (future)
```

**Правило:** направление — от `UI` к `Document`, от `Application` к сервисам. `Platform` — на уровне сервисов.

### 3.2. Фактические нарушения

| Нарушение | Где | Severity |
|---|---|---|
| **UI → Application\*** | `ViewCubeOverlay.cpp` include `app/Application.h` | 🔴 HIGH |
| **UI → CommandRegistry** | ✅ РАЗРЕШЕНО (тонкий интерфейс) | ✅ OK |
| **UI → OCCT** | `DocumentTree.cpp` (OcctView — **boundary problem**, не просто include!), `ViewCubeOverlay.cpp` (gp_Dir, OcctView) | 🔴 HIGH |
| **UI → GL напрямую** | `HomeScreen.cpp` (glGenTextures, glTexImage2D) | 🔴 HIGH |
| **UI → doc** | `DocumentTree.cpp`, `ContextMenu.cpp`, `dialogs/*` | 🟠 MEDIUM |
| **Application → OCCT напрямую** | `Application.cpp` — STEP/STL/TDF/BRepBuilder | 🔴 HIGH |
| **Theme → OcctView** | `ui/theme/*` — для градиента | 🔴 Убрать: только ThemeTokens |
| **util → OCCT** | `Utf8.h` включает `TCollection_ExtendedString.hxx` | 🟠 MEDIUM |

### 3.3. Правило зависимостей

- ❌ **UI → `Application*`** — запрещено (инверсия).
- ✅ **UI → `CommandRegistry` / `ICommandDispatcher`** — разрешено (тонкий интерфейс).
- ❌ **UI → OCCT напрямую** — запрещено.
- ✅ **UI → `IDocumentView`** (facade) — разрешено.

---

## 4. INCLUDE / IMPORT MAP (утечки через заголовки)

### 4.1. Утечка через `OcctView.h`

`OcctView.h` включает `AIS_InteractiveContext.hxx`, `OpenGl_GraphicDriver.hxx`, `V3d_View.hxx` → любой потребитель тянет OCCT.

**Потребители:** `Application.h`, `DocumentTree.cpp`, `ViewCubeOverlay.cpp`.

**Решение (будущее):** `OcctView` → интерфейс `IViewport` без OCCT-типов.

### 4.2. Утечка через `Application.h`

`Application.h` включает `OcctView.h` → OCCT.

**Потребители:** `Application.cpp` + `ViewCubeOverlay.cpp` (**инверсия**).

**Fix:** убрать include `Application.h` в `ViewCubeOverlay.cpp` (M-09).

---

## 5. APPLICATION.CPP DECOMPOSITION

| # | Группа | Функции | Строк | Перенос |
|---|---|---|---|---|
| **A** | Оркестрация | конструктор, деструктор, `Run()` | ~150 | **KEEP** |
| **B** | DocumentManager | `CreateNewDoc`, `AddDocFromFile`, `SwitchToDoc`, `CloseDoc`, `OnNewDocument`, `OnOpen` | ~150 | `app/DocumentManager` |
| **C** | FileOps | `OnSave`, `OnExportSTEP`, `OnExportSTL` | ~120 | `app/FileOps` + `doc/FileIO` |
| **D** | Undo/Redo (маршрутизация) | `OnUndo`, `OnRedo` | ~40 | `DocumentManager::routeUndo()` → `Document::undo()` |
| **E** | CommandRegistry | `InitCommands` + `myCommands` | ~40 | `app/CommandRegistry` |
| **F** | Telemetry | `UpdateTelemetry` | ~25 | `app/Telemetry` |
| **G** | Theme | `ApplyTheme` | ~70 | `ui/theme/ThemeManager` |
| **H** | Ribbon orchestration | `DrawRibbon` (обёртка) | ~50 | **KEEP** |
| **I** | View | `RefreshView`, `DrawViewCube` | ~50 | **SPLIT** |
| **J** | Sketch-логика | `OnSketchClick`, `EnterSketch`, `ExitSketch`, `ApplyExactLine`, `DrawPreviewInfo`, `RefreshSketchLines`, `IsPointOnExistingVertex`, `SnapToKnownPoint` | ~450 | **KEEP до Фазы 2** |
| **K** | Dialogs-обёртки | 8 тонких обёрток | ~50 | **KEEP** |
| **L** | Прочее | `RequestDelete`, `DrawMenuBar` (пусто), `DrawSketchOverlay` (пусто), `RibbonButton` (static B-12) | ~40 | **SPLIT** |
| **M** | OCCT-логика (размазана) | STEP/STL/TDF внутри B, C, I, J | ~300 | `doc/FileIO` |

---

## 6. STATE OWNERSHIP MAP

### 6.1. Классификация полей Application.h

| Поле | Категория | Владелец | Confidence | Risk |
|---|---|---|---|---|
| `myWindow` | Platform | Application | HIGH | LOW |
| `myOcctView` | Viewport | Viewport | MEDIUM | HIGH |
| `myViewCube` | UI State | ViewCube | MEDIUM | MEDIUM |
| `myTooltipState` | UI State | Tooltip (уже) | HIGH | LOW |
| `myIcons` | Resource | Application | MEDIUM | LOW |
| `myRibbonState` | UI State | Ribbon | HIGH | LOW |
| `mySearchBuffer[128]` | UI State | QAT | HIGH | LOW |
| `myRecentFiles` | App State | Application | MEDIUM | LOW |
| `myThumbnailPaths` | App State | DocumentManager | MEDIUM | MEDIUM |
| `myClipboardLabel` | Document | DocumentManager | HIGH | LOW |
| `myIsolationActive` | Document/UI | **DEFERRED** (semantic: state vs presentation) | MEDIUM | HIGH |
| `myIsolationBackup` | Document/UI | **DEFERRED** | MEDIUM | HIGH |
| `myVisibility` | Document/UI | **DEFERRED** | MEDIUM | HIGH |
| `myShowHomeScreen` | UI State | HomeScreen/AppShell | HIGH | LOW |
| `myHomeViewMode` | UI State | HomeScreen | HIGH | LOW |
| `myHomeSearch[128]` | UI State | HomeScreen | HIGH | LOW |
| `myRightPanelExpanded` | UI State | RightPanel | HIGH | LOW |
| `myPendingContextMenu` | Transient | ContextMenu | HIGH | LOW |
| `myContextMenuPos` | Transient | ContextMenu | HIGH | LOW |
| `myRenameState` | Dialog | RenameDialog | HIGH | LOW |
| `myDeleteConfirmState` | Dialog | DeleteConfirmDialog | HIGH | LOW |
| `myUnsavedState` | Dialog | UnsavedDialog | HIGH | LOW |
| `myPropertiesState` | Dialog | PropertiesDialog | HIGH | LOW |
| `myTreeWidth` | UI State | TreeSplitter/LeftPanel | HIGH | LOW |
| `myIsResizingTree` | UI State | TreeSplitter | HIGH | LOW |
| `myStartExpandedManually` | UI State | DocumentTree | HIGH | LOW |
| `mySettingsState` | Dialog | SettingsDialog | HIGH | LOW |
| `myNotImplementedState` | Dialog | NotImplementedDialog | HIGH | LOW |
| `myCurrentTheme` | App State | ThemeManager | HIGH | LOW |
| `myTheme` | Theme | ThemeManager | HIGH | LOW |
| `myLastRAMMB`, `myLastShapeCount` | Telemetry | Telemetry | HIGH | LOW |
| `myCommands` | CommandRegistry | CommandRegistry | HIGH | LOW |
| `myDocument` | Document | DocumentManager | HIGH | MEDIUM |
| `myDocuments` | Document | DocumentManager | HIGH | MEDIUM |
| `myActiveDocIndex` | Document | DocumentManager | HIGH | MEDIUM |
| `myNextProjectNumber` | Document | DocumentManager | HIGH | MEDIUM |
| `myProjectNames` | Document | DocumentManager | HIGH | MEDIUM |
| `myProjectPaths` | Document | DocumentManager | HIGH | MEDIUM |
| `myCurrentPath` | Document | DocumentManager | HIGH | MEDIUM |
| `myAddBoxState` | Dialog | AddBoxDialog | HIGH | LOW |
| `myActiveSketchLabel` | Sketch State | **DEFERRED** (возможно Document-editor) | MEDIUM | HIGH |
| `myHasSketchStart` | Transient | SketchController | HIGH | MEDIUM |
| `mySketchStartPoint` | Transient | SketchController | HIGH | MEDIUM |
| `myMouseX`, `myMouseY` | Transient | Viewport | HIGH | LOW |
| Sketch settings | UI Settings | SettingsState | MEDIUM | LOW |

### 6.2. Что НЕ переносим в Phase 1.6

- `myIsolationActive/Backup`, `myVisibility` — **DEFERRED** (семантика).
- `myActiveSketchLabel` — **DEFERRED**.
- Вся sketch-логика (J) — **KEEP**.

---

## 7. COMPONENT BOUNDARIES

### 7.1. LeftPanel

```
LeftPanel
  owns: TreePanel, VariablesPanel, OperationPanel, PanelTabs, PanelWidth

  knows:
    - LayoutRect
    - Commands (IDs)
    - IDocumentView (facade)
    - PanelState

  does NOT know:
    - OCCT
    - Viewport internals
    - Ribbon internals
    - StatusBar internals
```

### 7.2. Ribbon

```
Ribbon
  owns: RibbonTabs (activeTab), Groups, Buttons state

  knows:
    - RibbonState
    - ICommandDispatcher (IDs + canExecute + execute)
    - ThemeTokens
    - IconManager

  does NOT know:
    - Application*
    - CAD operations implementation
    - Document internals
    - OCCT
```

### 7.3. Viewport

```
Viewport
  owns: CameraState, SelectionState, InteractionState, SketchOverlay (позже)

  knows:
    - IDocumentView    ← интерфейс, не BreezeDocument
    - ViewportRect
    - Theme
    - Commands (IDs)

  does NOT know:
    - BreezeDocument   ← напрямую
    - OCAF
    - OCCT document internals
    - Ribbon, LeftPanel, StatusBar, ProjectTabs

  ⚠️ ВАЖНО: это РЕАЛЬНЫЙ boundary, не forwarding wrapper.
  Запрещено: 20 методов вида Viewport::Rotate() { myOcctView.Rotate(); }.
  Владеет: camera state, selection state, interaction state.
  Реализация — OCCT-renderer внутри.
```

### 7.4. Разделение FileOps / DocumentManager / FileIO

```
UI click Save
   ↓
FileOps.commandSave()          ← пользовательский уровень (команда)
   ↓
DocumentManager.save(activeDocument, path)   ← lifecycle + маршрутизация
   ↓
Document::save(path)           ← семантика операции
   ↓
IDocumentSerializer::write(doc, path)        ← физическое представление
   ↓
OCAFSerializer / BreezeSerializer (future)
```

**FileOps — только команды:**
- Save, Open, Export STEP, Export STL.
- Знает `DocumentManager`, не знает OCAF/OCCT.

**DocumentManager — lifecycle:**
- New, Open document, Close, Switch, Active document.
- Знает `BreezeDocument`, `IDocumentSerializer`.
- **НЕ знает FileDialog** (путь — параметр).
- **НЕ знает OCCT напрямую.**

**FileIO / IDocumentSerializer — физика:**
- `serialize(doc, path)`, `deserialize(path) → doc`.
- Не определяет, что такое «активный проект» или «кнопка Save».
- В будущем: `OCAFSerializer`, `BreezeSerializer` — отдельные реализации.

**Undo/Redo — семантика документа:**
- `BreezeDocument::undo()`, `BreezeDocument::redo()`.
- `DocumentManager::routeUndo()` — **маршрутизация на activeDocument**.
- Не содержит собственной системы Undo.

### 7.5. CommandRegistry

```
CommandRegistry
  owns:
    - ID → metadata (label, shortcut, canExecute, execute)

  knows:
    - Callbacks (что делать при execute)

  does NOT know:
    - Кто вызывает (Ribbon / QAT / Hotkey)
    - Кто реализует (Document / Geometry)

  ⚠️ ПРАВИЛО: хранит описание и связывает команду с исполнителем,
  но НЕ содержит бизнес-логику. execute() уходит в конкретный
  application/service/controller, а не содержит логику там же.
```

### 7.6. LayoutManager

```
LayoutManager
  owns:
    - расчёт rect
    - collision detection
    - overflow / out-of-bounds
    - layout constraints (minWidth / preferredWidth / maxWidth)

  knows:
    - window size
    - constraints компонентов

  does NOT know:
    - Содержимое компонентов
    - Что такое Ribbon/LeftPanel

  Порядок работы:
    resolve(constraints) → rects → validate(collisions)
```


---

## 8. TARGET COMPONENT MAP

### 8.1. Обязательные компоненты Phase 1.6

| Компонент | Назначение | Фаза |
|---|---|---|
| `app/Application.{cpp,h}` | оркестратор (эволюция в AppShell) | 1.6 |
| `app/CommandRegistry.{cpp,h}` | thin: ID → metadata + execute | 1.6.1 |
| `app/DocumentManager.{cpp,h}` | lifecycle документов | 1.6.2 |
| `app/Telemetry.{cpp,h}` | RAM/FPS/obj | 1.6.1 |
| `app/FileOps.{cpp,h}` | командный уровень Save/Open/Export | 1.6.3 |
| `doc/FileIO.{cpp,h}` | физическая запись/чтение через OCAF/OCCT | 1.6.3 |
| `ui/shell/LayoutManager.{cpp,h}` | расчёт layout rect | 1.6.4 |
| `ui/leftpanel/` | LeftPanel (обёртка Tree/Vars/Ops) | 1.6.5 |
| `ui/viewport/` | Viewport (обёртка OcctView) | 1.6.5 |

### 8.2. Компоненты позже (Фаза 2+)

| Компонент | Когда |
|---|---|
| `ui/leftpanel/OperationPanel` | Фаза 2 (плавающие окна операций) |
| `ui/viewport/SketchOverlay` | Фаза 2 (солвер) |

### 8.3. Не новые компоненты, а эволюция

| Что | Как |
|---|---|
| `Application → AppShell` | **НЕ создавать отдельный AppShell.** Существующий Application постепенно сужает ответственность. Переименование — после Phase 1.6, если действительно нужно. |

### 8.4. Целевая структура

```
src/
  app/
    Application.{cpp,h}         ← верхний уровень, эволюция в AppShell
    CommandRegistry.{cpp,h}     ← NEW
    DocumentManager.{cpp,h}     ← NEW
    Telemetry.{cpp,h}           ← NEW
    FileOps.{cpp,h}             ← NEW (тонкий: маршрутизация)
    Localization.{cpp,h}        ← было
    IconManager.{cpp,h}         ← было
    IconsFontAwesome.h          ← было

  ui/
    shell/
      LayoutManager.{cpp,h}     ← NEW
    leftpanel/
      LeftPanel.{cpp,h}         ← NEW
      TreePanel.{cpp,h}         ← из ui/panels/DocumentTree
    viewport/
      Viewport.{cpp,h}          ← NEW
    ribbon/                     ← было, KEEP
    viewcube/                   ← было, KEEP
    rightpanel/                 ← из ui/panels/RightPanel
    status/, home/, dialogs/, theme/, widgets/, qat/  ← было, KEEP

  doc/
    BreezeDocument.{cpp,h}      ← было
    BreezeApplication.{cpp,h}   ← было
    FileIO.{cpp,h}              ← NEW (STEP/STL/OCAF чтение-запись)

  platform/                     ← было, KEEP
  util/                         ← было, KEEP
  occt/                         ← было, KEEP (реализация Viewport)
  render/                       ← NEW (когда понадобится) — TextureCache, Texture
```

---

## 9. MIGRATION TABLE

| # | Что | Из | В | Confidence | Risk | Smoke Check |
|---|---|---|---|---|---|---|
| M-01 | Commands + InitCommands | Application | `app/CommandRegistry` | HIGH | LOW | build, launch, search, execute Undo |
| M-02 | Document lifecycle | Application | `app/DocumentManager` | HIGH | MEDIUM | New → Open → Save → Switch → Close |
| M-03 | Save/Export команды | Application | `app/FileOps` | MEDIUM | MEDIUM | Save, Export STEP, Export STL |
| M-04 | Undo/Redo маршрутизация | Application | `DocumentManager::routeUndo()` → `Document::undo()` | HIGH | LOW | Undo → Redo после операции |
| M-05 | Telemetry | Application | `app/Telemetry` | HIGH | LOW | build, launch, RAM/FPS видны |
| M-06 | ApplyTheme | Application | `ui/theme/ThemeManager` | MEDIUM | LOW | смена темы |
| M-07 | LayoutRect | Application + UI | `ui/shell/LayoutManager` | MEDIUM | HIGH | визуальная проверка, debug overlay |
| M-08 | Sketch-логика | Application | `ui/viewport/SketchOverlay` | LOW | HIGH | **ОТЛОЖЕНО** до Фазы 2 |
| M-09 | ViewCube include fix | `ViewCubeOverlay.cpp` | `ui/theme/ThemeTokens.h` | HIGH | LOW | build, launch |
| M-10 | HomeScreen GL | `HomeScreen.cpp` | `render/TextureCache` | HIGH | MEDIUM | Home screen с thumbnails |
| M-11 | RightPanel/TreeSplitter state | Application | RightPanel/TreeSplitter | HIGH | LOW | переключение панели, resize splitter |
| M-12 | Dialog states | Application | в сами диалоги | MEDIUM | LOW | каждый диалог: открыть, действие, закрыть |
| M-13 | Home state | Application | HomeScreen | HIGH | LOW | переключение home/документ |
| M-14 | Visibility/Isolation | Application | **DEFERRED** | MEDIUM | HIGH | — |
| M-15 | QAT search buffer | Application | QAT | HIGH | LOW | поиск команд |

**Confidence** — уверенность в правильности целевого места.
**Risk** — опасность изменения для существующего поведения.

---

## 10. ORDER OF WORK

### Phase 1.6.0 — быстрые архитектурные fixes (30-60 мин)

- **M-09** — `ViewCubeOverlay.cpp` include fix.
- **M-10** — `HomeScreen` GL → `render/TextureCache`.
- `DocumentTree.cpp` — **boundary problem**, не просто include. Убрать зависимость от `OcctView`; если технически невозможно в рамках текущего шага — зафиксировать как deferred boundary.
- **Smoke:** build + launch + визуальная проверка ViewCube + Home.

### Phase 1.6.1 — независимые утилиты (полдня)

- **M-05** Telemetry.
- **M-01** CommandRegistry (thin).
- **Smoke:** build, launch, search command, Undo/Redo.

### Phase 1.6.2 — DocumentManager (1-2 дня)

- **M-02** (lifecycle) + **M-04** (Undo/Redo маршрутизация).
- **Smoke:** New → Open → Save → Switch → Close + Undo/Redo на 3 документах.

### Phase 1.6.3 — FileOps + FileIO (1-2 дня)

- **M-03** (Save + ExportSTEP + ExportSTL).
- Требует: вынести OCCT-логику из Application.
- **Smoke:** экспорт в STEP, STL; сохранение и повторное открытие.

### Phase 1.6.4 — LayoutManager (2-3 дня)

- **M-07** — общий layout.
- **Затрагивает P0:** CR-020, CR-405, CR-059, CR-564, CR-565, CR-606.
- **Smoke:** визуальная проверка, debug overlay (collision count).

### Phase 1.6.5 — UI State ownership (1-2 дня)

- **M-06, M-11, M-12, M-13, M-15**.
- **Smoke:** каждый компонент: открыть, подействовать, закрыть.

### Phase 1.6.X — КРИТЕРИЙ ВЫХОДА

См. §12.

---

## 11. BUILD BOUNDARY (перспектива, НЕ сейчас)

**Не превращаем сейчас в CMake-библиотеки.** Помечаем перспективу:

| Компонент | Public API (что наружу) | Будущий target |
|---|---|---|
| `app/Application` | lifecycle only | executable |
| `app/CommandRegistry` | `registerCommand`, `execute(id)` | `lib:app` |
| `app/DocumentManager` | `newDoc`, `open`, `save`, `close`, `active` | `lib:app` |
| `app/Telemetry` | `update`, `getRAM`, `getFPS` | `lib:app` |
| `app/FileOps` | `commandSave`, `commandOpen`, `commandExport` | `lib:app` |
| `ui/ribbon` | `draw`, `setState`, `dispatchCommand` | `lib:ui_ribbon` |
| `ui/leftpanel` | `draw`, `setActivePanel` | `lib:ui_leftpanel` |
| `ui/viewport` | `draw`, `getCamera`, `setCamera`, `getSelection` | `lib:ui_viewport` |
| `ui/shell/LayoutManager` | `calculate`, `getRect(id)`, `validate` | `lib:ui_shell` |
| `doc/BreezeDocument` | `undo`, `redo`, `addFeature`, `getHistory` | `lib:doc` |
| `doc/FileIO` | `serialize`, `deserialize` | `lib:doc` |
| `occt/OcctView` | `init`, `draw`, `resize`, `pick` | `lib:occt` |
| `platform` | — (сервисный слой) | `lib:platform` |
| `util` | — (утилиты) | `lib:util` |
| `render` | `createTexture`, `bindTexture` | `lib:render` |

**Правило:** модульность начинается не с папок, а с **Public API** — что видно снаружи.

**Правило:** физическое разделение — только когда есть **реальная польза** (компиляция, тесты, изоляция).

---

## 12. INVARIANTS

### 12.1. Application

- ✅ Lifecycle + композиция + high-level routing.
- ❌ НЕ реализует внутренности Ribbon, LeftPanel, Tree, Viewport, StatusBar, Dialog internals.
- ❌ НЕ содержит OCCT-логику напрямую.
- ❌ НЕ держит UI state, принадлежащий компонентам.

**Не имеет значения, сколько строк в Application.** Имеет значение: содержит ли он только композицию и lifecycle.

### 12.2. Ribbon

- ✅ Отображает вкладки, группы, кнопки.
- ✅ Только dispatches commands.
- ❌ НЕ знает Application*, OCCT, Document internals, Viewport.
- ✅ Знает `ICommandDispatcher` / `CommandRegistry`.

### 12.3. Viewport

- ✅ Render interaction, camera, selection.
- ✅ Знает `IDocumentView` (facade).
- ❌ НЕ знает `BreezeDocument`, OCAF, OCCT document internals.
- ❌ НЕ управляет Ribbon, LeftPanel, StatusBar.
- 🔴 **Реальный boundary, не forwarding wrapper.** Запрещено: 20 методов вида `Viewport::Rotate() { myOcctView.Rotate(); }`.
- ✅ Владеет: camera state, selection state, interaction state. Реализация — OCCT-renderer внутри.

### 12.4. LeftPanel

- ✅ Отвечает за активную панель (Tree / Vars / Ops).
- ✅ Tree не знает Ops, Ops не знает Tree.
- ❌ НЕ знает Ribbon, Viewport internals.

### 12.5. DocumentManager

- ✅ Lifecycle: New/Open/Save/Close, activeDocument.
- ✅ Знает `IDocumentSerializer` (FileIO).
- ✅ **Маршрутизирует** Undo/Redo на `activeDocument.undo()` — не содержит свою систему Undo.
- ❌ НЕ знает FileDialog (путь — параметр).
- ❌ НЕ содержит UI layout.
- ❌ НЕ работает с OCCT напрямую.

### 12.6. CommandRegistry

- ✅ ID → metadata + execute callback.
- ❌ НЕ содержит реализацию команд.
- ❌ НЕ знает, кто вызывает.

### 12.7. LayoutManager

- ✅ Расчёт rect, collision, overflow, out-of-bounds.
- ✅ **Layout constraints:** minWidth / preferredWidth / maxWidth для каждого компонента.
- ✅ `resolve(constraints) → rects → validate(collisions)`.
- ❌ НЕ содержит бизнес-логику компонентов.
- ❌ НЕ содержит `if (width < 1000) ...` — всё через constraints.

### 12.8. Общие правила

- UI **НЕ** зависит от OCCT напрямую.
- UI **НЕ** работает с GL напрямую.
- Никаких циклов в зависимостях.
- **UI → Application\*** запрещён. **UI → ICommandDispatcher / CommandRegistry** разрешён.
- **Theme → OcctView** запрещён. Только ThemeTokens.
- **Atomicity:** `change → build → smoke → commit`. Один смысловой шаг = один коммит.
- **Warnings:** не увеличивать baseline (before: N, after: ≤ N).

---

## 13. ФИНАЛЬНЫЙ КРИТЕРИЙ

### Плохой результат

```
Application.cpp → 15 файлов по 100 строк
    ↓
те же зависимости
    ↓
хуже для понимания
```

### Хороший результат

```
Application (тонкий оркестратор)
├── CommandRegistry
├── DocumentManager
├── Telemetry
├── FileOps + FileIO
├── LayoutManager
└── UI Components
    ↓
чёткие границы
    ↓
локальные зависимости
```

### Phase 1.6 DONE, когда:

- [ ] Application отвечает только за lifecycle + композицию + routing.
- [ ] CommandRegistry, DocumentManager, Telemetry, FileOps, FileIO, LayoutManager существуют.
- [ ] LeftPanel, Viewport — изолированы.
- [ ] UI не создаёт новых зависимостей на OCCT.
- [ ] UI не работает с GL напрямую.
- [ ] Нет инверсий слоёв.
- [ ] Layout collision validation работает.
- [ ] Не появилось новых warnings (baseline: N, after: ≤ N).
- [ ] Существующее поведение сохранено.
- [ ] Пройден Smoke test.

**Не требуется «идеальная архитектура».** Требуется **достаточно хорошая, чтобы двигаться дальше**.

После закрытия Phase 1.6 → **сразу Sketch MVP**.

---

## Приложение A: что НЕ делать

- ❌ Не создавать Manager без ответственности.
- ❌ Не переносить код только ради уменьшения `Application.cpp`.
- ❌ Не выносить в библиотеки без реальной пользы.
- ❌ Не трогать Sketch-логику до Фазы 2.
- ❌ Не менять OCAF до Фазы 3.
- ❌ Не менять UI design в рамках Phase 1.6.
- ❌ Не создавать `AppShell` как отдельный класс — эволюция Application.
- ❌ Не закреплять `TextureCache` в `ui/widgets` если он renderer-specific → в `render/`.
- ❌ Не расширять M-09 (или любую другую задачу) по ходу работы.
- ❌ Не считать forward declaration лечением `DocumentTree → OcctView` — это boundary problem.

## Приложение B: связь с аудитом

Карта пересекается с ~120 замечаниями из `docs/review/`:
- Phase 1.6 **ожидаемо затронет** ~50 замечаний.
- **Окончательный статус** определяется после переклассификации P0-A/B/C.
- Порядок работ: P0_dialogs → P0_app → P0_doc → P0_panels/ribbon.

