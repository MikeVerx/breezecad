# P0 — КРИТИЧНО — часть 1 из 5

**Записей в части:** 48

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-001. `std::function` пересоздаётся 60 раз/сек

**Где:** `Application.cpp::DrawRibbon()`, `RibbonContext.h`
**Статус:** OPEN
**Категория:** производительность (OPT)

**Проблема:** каждое поле `ctx.drawQat`, `ctx.clearPreviewLine`, `ctx.exitSketch` — это closure + heap-аллокация на `std::function`. Создаётся каждый кадр. Внутри `ctx.drawQat` ещё `QatContext` с 10 колбэками.

**Замеры:**
- ~15 closure'ов × 60 FPS = **900 heap-аллокаций/сек** только на ribbon
- +10 колбэков `qat.*` = ещё 600/сек

**Цена:** для целевого железа (4 ГБ, iGPU, ARM A18) — недопустимо.

**Фикс:**
1. `RibbonContext myRibbonContext` — **member** `Application`.
2. Инициализация — один раз в конструкторе или `InitCommands`.
3. В `DrawRibbon()` — только обновление мгновенных полей.

**Оценка:** 30–40 минут.

---


### CR-002. `static int dummy = 0` — RULES п.20 + мина

**Где:** `RibbonButton.cpp::SingleButton`, `SketchTab.cpp::DrawSketchTab`
**Статус:** OPEN
**Категория:** RULES п.20

**Проблема:**
```
static int dummy = 0;
const char* opts[] = { key };
return Button(icons, id, opts, 1, dummy, ...);
```

**Почему мина:**
- `dummy` общий для всех вызовов `SingleButton`.
- Если `optCount > 1` когда-то — popup вернёт `3`, `dummy` изменится и **никогда не вернётся в 0**.

**В `SketchTab.cpp`** — та же проблема, `static int dummy` идёт в 20+ вызовов.

**Фикс:** убрать `static`. `int dummy = 0` на стеке — ноль цены.
**Заодно:** занести как **B-17** в `REFACTOR_NOTES` (в комментарии указано, но в файле нет).

**Оценка:** 10 минут.

---


### CR-003. Хардкод цветов в `RibbonButton.cpp` — RULES п.30

**Где:** `RibbonButton.cpp:34-42`
**Статус:** OPEN
**Категория:** RULES п.30

**Проблема:**
```
if (isLight) {
    bgHover  = ImVec4(0.760f, 0.800f, 0.860f, 0.6f);
    bgActive = ImVec4(0.640f, 0.720f, 0.840f, 0.8f);
} else {
    bgHover  = ImVec4(0.35f, 0.55f, 0.85f, 0.35f);
    bgActive = ImVec4(0.35f, 0.55f, 0.85f, 0.55f);
}
```

RULES п.30: «Всё окрашивается от темы. Никаких хардкод-цветов в UI-коде.»

**Фикс:**
1. В `ThemeTokens` — `ribbonBtnHover`, `ribbonBtnActive`, `ribbonBtnHoverDisabled`, `ribbonBtnActiveDisabled`, `arrowBg`, `arrowBgDisabled`.
2. В `RibbonButton.cpp` — тянуть из темы.
3. Проверить 3 темы (Светлая / Тёмная / Авто).

**Оценка:** 40–50 минут + проверка.

---


### CR-004. `LineStyle*` в `RibbonContext` — утечка слоя doc в UI

**Где:** `RibbonContext.h`, `SketchTab.h`
**Статус:** OPEN
**Категория:** архитектура (`ARCHITECTURE.md §2`)

**Проблема:**
```
#include "doc/BreezeDocument.h"   // LineStyle
...
LineStyle* currentLineStyle = nullptr;
```

UI-модуль жёстко зависит от `doc/BreezeDocument.h`. Если `LineStyle` переедет в `kernel/` — сломается ribbon.

**Фикс (варианты):**
- **A.** `int* currentLineStyleIdx` — UI знает 0/1/2.
- **B.** Колбэки `setLineStyle(int)` + `getLineStyle()` — UI не знает семантику.
- **C.** Enum в `util/`.

Рекомендую **B** — чище по слоям.

**Оценка:** 20 минут.

---



### CR-049. `ViewTab.cpp` — три одинаковых пункта в выпадашке «Панорамирование»

**Где:** `ViewTab.cpp:21`
**Статус:** OPEN
**Категория:** функциональный баг

**Проблема:**
```
static const char* panOpts[] = { "tool.pan", "tool.pan", "tool.pan" };
```

Меню открывается — пользователь видит «Панорамирование / Панорамирование / Панорамирование».

**Фикс:** оставить один пункт `{ "tool.pan" }` или заменить на реальные варианты (панорама / суперштурвал отключён / …).

**Оценка:** 5 минут.

---


### CR-050. `ViewTab.cpp` — `optCount=1` при массиве из 3

**Где:** `ViewTab.cpp:22`

**Проблема:**
```
Button(icons, "nav_pan", panOpts, 1, dummy, ICON_PAN, true, true, ...);
```
`optCount=1` → `hasArrow = (1 > 1) = false` → **стрелка ▼ не появляется**, хотя массив из 3.

Связано с CR-049. Оба фикса — заодно.

---


### CR-051. `ViewTab.cpp` — `int zoomIdx = 0;` сбрасывается каждый кадр

**Где:** `ViewTab.cpp:28, 36`

**Проблема:**
```
int zoomIdx = 0;   // локальная
Button(icons, "nav_zoom", zoomOpts, 4, zoomIdx, ...);
```

Пользователь выбирает «Показать всё» → следующий кадр → `zoomIdx = 0` → снова «Зумировать». **Выбор не сохраняется.**

**Фикс:** перенести в `RibbonState` как `zoomToolIndex`, `orbitToolIndex`.

---


### CR-052. `ViewTab.cpp` — несогласованность `state` vs локальных `idx`

**Где:** `ViewTab.cpp`

| Кнопка | Источник индекса | Сохраняется? |
|---|---|---|
| `view_style` | `state.displayStyleIndex` | ✅ |
| `view_bg` | `state.backgroundIndex` | ✅ |
| `nav_zoom` | `int zoomIdx = 0` | ❌ |
| `nav_orbit` | `int orbIdx = 0` | ❌ |
| `nav_pan` | `dummy` | ❌ |

**Фикс:** все индексы — в `RibbonState`.

---


### CR-053. `ViewTab.cpp` — `win_switch` использует ключ от `win_new`

**Где:** `ViewTab.cpp:97-100`

**Проблема:**
```
SingleButton(icons, "win_switch", "tool.new_window", ...);
SingleButton(icons, "win_new",    "tool.new_window", ...);
```

Обе кнопки показывают tooltip «Новое окно». Должно быть `tool.switch_window` для первой.

**Фикс:** проверить `Localization.cpp`, добавить/исправить ключ.

---


### CR-055. `Application.cpp::Run()` — `static bool prevRMB` в member-функции

**Где:** `Application.cpp:1139-1140`

**Проблема:**
```
static bool  prevRMB = false;
static ImVec2 rmbStartPos(0, 0);
```

RULES п.20 нарушен. Сейчас работает (одно окно), но при создании второго `Application` — общий `prevRMB`. **Занести как B-18.**

**Фикс:** member-поля `myPrevRMB`, `myRmbStartPos`.

---


### CR-056. `Application.cpp::Run()` — `static double lastPreviewX/Y` — та же проблема

**Где:** `Application.cpp:1180`
**Фикс:** member-поля.

---


### CR-057. `Application.cpp::Run()` — мёртвые переменные

**Где:** `Application.cpp:1095-1097`

```
bool prevDelete = false;   // не используется
bool prevCtrlS  = false;   // не используется
bool prevCtrlO  = false;   // не используется
```

**Фикс:** удалить.

---


### CR-058. `Application.cpp::Run()` — магические числа

**Где:** `Application.cpp:1162, 1185`

```
if (dx*dx + dy*dy < 25.0f)             // 5px² — что такое 5?
if (dxp*dxp + dyp*dyp > 9.0)           // 3px² — комментарий есть
```

**Фикс:** `constexpr float kRmbClickToleranceSq = 25.0f;` + `kPreviewMoveToleranceSq = 9.0f;`.

---


### CR-059. `Application.cpp::Run()` — магия зон вьюпорта

**Где:** `Application.cpp:1146-1149`

```
bool inScene = io.MousePos.x > myTreeWidth + 10
            && io.MousePos.y > 165              // ← что такое 165?
            && io.MousePos.x < io.DisplaySize.x - 320   // ← 320?
            && io.MousePos.y < io.DisplaySize.y - 70;   // ← 70?
```

Все четыре — magic. `165` — высота ribbon+tabs (в UI_LAYOUT = 142). Снова расхождение с каноном.

**Фикс:** `constexpr` + актуализация UI_LAYOUT.

---


### CR-060. `Application.cpp::Run()` — `try {` без `catch` (проверить)

**Где:** `Application.cpp:1101`

Открывается `try {`, но в видимом фрагменте нет `catch`. Проверить ниже по файлу — есть ли.

**Если нет** — компилятор не пропустит. Если есть — **что там?** Глушится ли исключение? Логируется?

---


### CR-061. `Application.cpp::Run()` — паттерн `winW/winH/fbW/fbH` дублируется 3+ раз

**Где:** `Application.cpp:1156-1160`, `1187-1191`, и ещё ниже

```
int winW = 1, winH = 1, fbW = 1, fbH = 1;
glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
int px = (int)(x * fbW / winW);
int py = (int)(y * fbH / winH);
```

**Фикс:** helper `ScreenToFramebuffer(double x, double y, int& px, int& py)`.

---


### CR-062. `Application.cpp::Run()` — прямые `glfwGetKey`/`glfwGetMouseButton` вместо `ImGuiIO`

**Где:** `Application.cpp:1134, 1140, 1197-1198`

```
glfwGetKey(..., GLFW_KEY_F11)
glfwGetMouseButton(..., GLFW_MOUSE_BUTTON_RIGHT)
glfwGetKey(..., GLFW_KEY_LEFT_SHIFT)
```

ImGui уже это знает: `io.KeyShift`, `io.MouseDown[1]`, `io.KeysDown[GLFW_KEY_F11]`. **Меньше GLFW-вызовов в hot-path.**

**Фикс:** заменить на `io.*`.

---



### CR-086. `Application` конструктор — молчаливый выход при провале окна

**Где:** `Application.cpp:135`
```
myWindow = std::make_unique<GlfwWindow>(...);
if (!myWindow->GetNativeWindow()) return;   // ← return из конструктора!
```

Если окно не создалось — конструктор завершается, но **объект создан в полу-состоянии**. `Run()` вызовется и упадёт.

**Фикс:** либо `throw std::runtime_error`, либо явное поле `bool myInitFailed`.

---


### CR-087. `ApplyTheme` вызывается **до** `LoadFonts`

**Где:** `Application.cpp:151-153`
```
ApplyTheme(myCurrentTheme);          // ← первый раз, шрифты не готовы
UI::Theme::LoadFonts();              // ← загружаем шрифты
...
ApplyTheme(myCurrentTheme);          // ← второй раз, ок
```

Первый вызов — **возможно, бесполезен**. Надо проверить, что делает `UI::Theme::ApplyTheme` — если он трогает размеры шрифтов, первый вызов может дать неверные значения.

**Фикс:** либо убрать первый, либо обосновать комментарием.

---


### CR-088. Пустой `catch (...)` глотает все ошибки

**Где:** `Application.cpp:161`
```
try {
    if (!myOcctView->Init(...)) { myOcctView.reset(); return; }
} catch (...) { myOcctView.reset(); return; }
```

**Молчаливый `return`** + `catch(...)` без логирования. При любой ошибке OCCT приложение тихо не запускается. Пользователь видит пустой чёрный экран.

**Фикс:** как минимум `std::cerr << "OcctView::Init failed\n";`. Как максимум — `std::exception_ptr` + rethrow.

---


### CR-089. `ApplyTheme`: magic `3` для system

**Где:** `Application.cpp:133`
```
myCurrentTheme = (theme == 3) ? 1 : theme;
```
`3` = System. Нужен `enum class Theme { Light=0, Dark=1, System=2 };`. Заодно связать с `ThemeTokens.h`.

---


### CR-090. `~Application()` — порядок shutdown может дать UB

**Где:** `Application.cpp:199-203`
```
Application::~Application() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
```

**Проблемы:**
1. `myOcctView` — `unique_ptr`, dtor вызовется **после** тела деструктора. То есть **после** `ImGui shutdown` и `DestroyContext`. Если `OcctView::~OcctView()` дёргает `ImGui` или `ImGui` state — UB.
2. `myIcons` — не освобождает GL-текстуры. Утечка при перезапуске.
3. `myWindow` — тоже после. Если `GlfwWindow::~GlfwWindow()` дёргает `glfwTerminate()` — а `ImGui` уже сдох. Порядок критичен.

**Фикс:** явный сброс `myOcctView.reset(); myWindow.reset();` **в начале** деструктора, **до** ImGui shutdown.

---


### CR-091. `IconManager::LoadAll("assets/icons")` — хардкод пути

**Где:** `Application.cpp:140`
RULES п.23: «Никаких хардкодов путей. `std::filesystem` или glfw-хелперы.»

**Фикс:** `std::filesystem::path exeDir = ...` + `exeDir / "assets" / "icons"`.

---


### CR-092. Рекурсивная `std::function` в `RefreshView` — heap на каждой итерации

**Где:** `Application.cpp:215`
```
std::function<void(const TDF_Label&)> walk = [&](const TDF_Label& l) {
    ...
    walk(it.Value());   // ← рекурсия через std::function = heap alloc
};
```

Для дерева из 1000 узлов — **1000 heap-аллокаций** на каждый `RefreshView`. `RefreshView` вызывается после каждой операции. **Плохо.**

**Фикс:** рекурсивная лямбда через `auto&& self` (Y-combinator) или free-функция.

---


### CR-093. `RefreshView`: `std::string key = LabelKey(l);` — heap на каждый узел

**Где:** `Application.cpp:224`
То же — 1000 heap-аллокаций на обход.

**Фикс:** `myVisibility` по `TDF_Label` напрямую или по `uint64_t` hash.

---


### CR-094. `RefreshView`: `myOcctView->ClearAllShapes()` + полный обход каждый раз

**Где:** `Application.cpp:205, 242`
После каждой операции — **полная переотрисовка** всего дерева. Для 1000 фич — O(N) на операцию → O(N²) на сессию.

**Фикс:** инкрементальный diff — обновлять только изменённые узлы.

---


### CR-095. `InitCommands`: `[]{}` пустые лямбды — команды без действия

**Где:** `Application.cpp:1050-1053, 1058-1060`
```
add("sketch.rect", "Прямоугольник", "R", []{});
add("sketch.circle", "Окружность", "C", []{});
```

Команды есть в поиске, но клик — **ничего не происходит**. Пользователь в недоумении.

**Фикс:** либо реальные действия, либо `nullptr` + пометка «не реализовано» в UI.

---


### CR-096. `InitCommands`: `sketch.line` дублирует код из `SketchTab.cpp:77`

**Где:** `Application.cpp:1045-1049` vs `SketchTab.cpp:77-81`

**Копипаста** трёх строк:
```
myCurrentLineStyle = LS_Normal;
myHasSketchStart = false;
if (myOcctView) myOcctView->ClearPreviewLine();
```

**Фикс:** вынести в `Application::ResetSketchLineTool()`, вызывать из обоих мест. Связано с CR-009.

---


### CR-097. `InitCommands`: `std::cout` — BUG-A4 (кириллица)

**Где:** `Application.cpp:1068`
`std::cout << "InitCommands: ..."` — если когда-нибудь добавишь кириллицу в лог, получишь CP866-кракозябры (BUG-A4).

**Фикс:** единый logger или `setlocale`.

---


### CR-098. `UpdateTelemetry`: `PROCESS_MEMORY_COUNTERS pmc;` — не инициализирован

**Где:** `Application.cpp:1077`
```
PROCESS_MEMORY_COUNTERS pmc;   // ← мусор
if (GetProcessMemoryInfo(...)) { myLastRAMMB = ...; } else { myLastRAMMB = 0.0; }
```
Формально ок (myLastRAMMB перезапишется), но паттерн плохой. **Фикс:** `= {};`.

---


### CR-099. `UpdateTelemetry`: `(1024.0 * 1024.0)` — magic

**Фикс:** `constexpr double kBytesPerMB = 1024.0 * 1024.0;`.

---


### CR-100. `UpdateTelemetry`: `myLastShapeCount` — тип?

**Где:** `Application.cpp:1089`
`myLastShapeCount = myOcctView->GetShapeCount();`
`GetShapeCount()` возвращает `int` или `size_t`? Если второе — обрезка. Проверить.

---


### CR-101. `InitCommands`: hotkey «R» для Прямоугольника, но нет реальной обработки

**Где:** `Application.cpp:1050`
Хоткеи в командах есть, но **кто их обрабатывает**? В `Run()` — не видно. Возможно, ImGui shortcuts или внешний код. Проверить.

---


### CR-102. Обработчики `myWindow->SetXxxCallback([this]...)` захватывают `this`

**Где:** `Application.cpp:180-193`
Лямбды захватывают `this`. Если окно живёт **дольше** Application (например, во время destruction) — callback сработает на мёртвом `this`. **UB.**

**Фикс:** либо отписываться в `~Application`, либо `weak_ptr`-схема.

---


### CR-103. `InitCommands`: команды не локализованы через ключи

**Где:** `Application.cpp:1040-1066`
```
add("file.new", "Создать", "Ctrl+N", ...);   // ← русский хардкод
```
`name` идёт хардкодом. Локализация обходится. При переключении на EN — команды останутся на русском.

**Фикс:** `add("file.new", "cmd.file.new", ...)` + `Tr()`.

---


### CR-104. `RefreshView`: `LabelKey(l)` возвращает `std::string`

**Где:** `Application.cpp:224`
Уже в CR-093. Плюс: что делает `LabelKey`? Ищет по имени или по `Entry()`? Если по имени — там кириллица (BUG-A5, `TCollection_AsciiString`).

---


### CR-105. `RefreshView`: `myOcctView->Redraw()` в конце — синхронно

**Где:** `Application.cpp:241`
`RefreshView` вызывается часто (после каждой операции). `Redraw()` — блокирующий. **Фикс:** отложить до следующего кадра, вызывать в `Run()`.

---



### CR-111. `SwitchToDoc` — `*myOcctView` без проверки на nullptr

**Где:** `Application.cpp:296`
```
UI::Home::CaptureCurrentThumbnail(*myOcctView, myActiveDocIndex, myThumbnailPaths);
```
Если `myOcctView == nullptr` (не инициализировался, был reset) — **UB, краш**.

**Фикс:** `if (myOcctView) UI::Home::CaptureCurrentThumbnail(...);`

**Оценка:** 1 минута.

---


### CR-112. `OnSave` не обновляет `myProjectPaths[myActiveDocIndex]`

**Где:** `Application.cpp:363-378`

**Проблема:**
- Пользователь создаёт новый документ → `myProjectPaths[myActiveDocIndex] = ""` (в `CreateNewDoc`).
- Жмёт Ctrl+S → диалог «Сохранить как» → сохраняет в `D:/foo.cbf`.
- `myCurrentPath = path;` ✅
- **`myProjectPaths[myActiveDocIndex]` остаётся `""`** ❌

**Что ломается:**
- В списке проектов путь пустой.
- `UI::Home::AddRecentFile(myRecentFiles, path)` работает, но `myProjectPaths` рассинхронизирован.
- При следующем сохранении снова спросит путь.

**Фикс:** `myProjectPaths[myActiveDocIndex] = path;` после успешного `SaveDocument`.

**Оценка:** 2 минуты.

---


### CR-113. `OnSave` не обновляет `myProjectNames[myActiveDocIndex]`

**Где:** `Application.cpp:363-378`

По аналогии с CR-112. Пользователь сохранил `Project1` как `Деталь_фланца.cbf` — имя проекта остаётся `Project1`. **UX-баг.**

**Фикс:** `myProjectNames[myActiveDocIndex] = <имя из path>;` (как в `AddDocFromFile`).

**Оценка:** 5 минут.

---


### CR-116. `Interface_Static::SetCVal` — глобальный статик OCCT

**Где:** `Application.cpp:392-393`

```
Interface_Static::SetCVal("write.step.unit", "MM");
Interface_Static::SetCVal("write.step.schema", "AP214");
```

`Interface_Static` — **глобальное состояние OCCT-процесса**, не per-instance. Если когда-нибудь будут параллельные экспорты (или другой код OCCT, читающий эти параметры) — гонка.

**Дополнительно:** эти параметры — **дефолтные**, `SetCVal` избыточен. Проверить.

**Фикс:** если нужен явный AP214 — использовать `STEPControl_Writer` с параметрами per-writer (если API позволяет). Иначе — `SetCVal` **один раз** при инициализации, не каждый экспорт.

**Оценка:** 10 минут (проверить API).

---


### CR-114. `myVisibility.clear()` при switch/close — теряется состояние видимости

**Где:** `Application.cpp:305, 352, 466`

Пользователь:
1. В документе A скрыл ось X (`myVisibility["..."] = false`).
2. Переключился в документ B (`SwitchToDoc` → `myVisibility.clear()`).
3. Вернулся в A.
4. **Ось X снова видна.**

**Фикс:** `myVisibility` должен быть **per-document**, а не глобальный. Хранить в `BreezeDocument` или в `std::map<int, map<string, bool>>`.

**Оценка:** 30 минут.

---


### CR-115. `CloseDoc` не сохраняет превью перед закрытием

**Где:** `Application.cpp:318-362`

`SwitchToDoc` вызывает `CaptureCurrentThumbnail`, но `CloseDoc` — **нет**. Если пользователь закрывает документ, превью для него не сохраняется → в Home Screen карточка без обновлённого превью.

**Фикс:** вызвать `CaptureCurrentThumbnail` в начале `CloseDoc`.

**Оценка:** 5 минут.

---


### CR-117. `OnExportSTL` — тесселяция 0.1 хардкод + блокирует UI

**Где:** `Application.cpp:434`
```
BRepMesh_IncrementalMesh mesh(comp, 0.1);   // ← 0.1 мм, hardcoded
```

**Проблемы:**
1. `0.1` — magic. На большой детали (500мм) слишком мелко → тормоза; на мелкой — слишком крупно.
2. **Блокирует UI** на время тесселяции. RULES п.26: всё >1 сек — с прогресс-баром.
3. Не спросили у пользователя точность.

**Фикс:**
- Вынести `0.1` в константу или в параметр диалога.
- Тесселяция — в `std::async`, с прогрессом.

**Оценка:** 1 час (правильно).

---


### CR-118. `OnExportSTL` — нет прогресс-бара

Связано с CR-117. RULES п.26: «Прогресс-бары для всего, что >1 сек».

**Оценка:** входит в CR-117.

---


### CR-119. `Standard_False` в `OnExportSTL`

**Где:** `Application.cpp:441`
```
writer.ASCIIMode() = Standard_False;
```

TECH-DEBT-005. **Фикс:** `false`.

**Оценка:** 1 минута.

---


### CR-122. `AddDocFromFile` / `OnOpen` — нет проверки на дубликат

**Где:** `Application.cpp:269, 452`

Пользователь открывает `foo.cbf` дважды → два `BreezeDocument` с одним файлом. При сохранении один перезапишет другой.

**Фикс:** проверить `myProjectPaths` на наличие `path`, если есть — `SwitchToDoc`.

**Оценка:** 10 минут.

---


### CR-124. `static bool RibbonButton` — хардкод цветов (B-12)

**Где:** `Application.cpp:531-548`

```
if (active) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 90, 140, 255));
} else {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(120, 30, 30, 255));
    ...
}
```

Это тот самый **B-12 debug-helper, KEEP**. Но цвета — **хардкод** (RULES п.30).

**Решение:** оставить функцию, но цвета — из темы. Либо пометить как debug-only, не участвующий в продакшене.

**Оценка:** 10 минут.

---


