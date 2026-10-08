# BreezeCAD — Review: P0 / app

**Записей:** 35

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-057. `Application.cpp::Run()` — мёртвые переменные
**Включает (merged):** CR-591, CR-617, CR-686

**Где:** `Application.cpp:1095-1097`

```
bool prevDelete = false;   // не используется
bool prevCtrlS  = false;   // не используется
bool prevCtrlO  = false;   // не используется
```

**Фикс:** удалить.

---

---

### CR-060. `Application.cpp::Run()` — `try {` без `catch` (проверить)

**Где:** `Application.cpp:1101`

Открывается `try {`, но в видимом фрагменте нет `catch`. Проверить ниже по файлу — есть ли.

**Если нет** — компилятор не пропустит. Если есть — **что там?** Глушится ли исключение? Логируется?

---

---

### CR-061. `Application.cpp::Run()` — паттерн `winW/winH/fbW/fbH` дублируется 3+ раз
**Включает (merged):** CR-312, CR-376

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

---

### CR-089. `ApplyTheme`: magic `3` для system

**Где:** `Application.cpp:133`
```
myCurrentTheme = (theme == 3) ? 1 : theme;
```
`3` = System. Нужен `enum class Theme { Light=0, Dark=1, System=2 };`. Заодно связать с `ThemeTokens.h`.

---

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

---

### CR-098. `UpdateTelemetry`: `PROCESS_MEMORY_COUNTERS pmc;` — не инициализирован

**Где:** `Application.cpp:1077`
```
PROCESS_MEMORY_COUNTERS pmc;   // ← мусор
if (GetProcessMemoryInfo(...)) { myLastRAMMB = ...; } else { myLastRAMMB = 0.0; }
```
Формально ок (myLastRAMMB перезапишется), но паттерн плохой. **Фикс:** `= {};`.

---

---

### CR-101. `InitCommands`: hotkey «R» для Прямоугольника, но нет реальной обработки

**Где:** `Application.cpp:1050`
Хоткеи в командах есть, но **кто их обрабатывает**? В `Run()` — не видно. Возможно, ImGui shortcuts или внешний код. Проверить.

---

---

### CR-102. Обработчики `myWindow->SetXxxCallback([this]...)` захватывают `this`

**Где:** `Application.cpp:180-193`
Лямбды захватывают `this`. Если окно живёт **дольше** Application (например, во время destruction) — callback сработает на мёртвом `this`. **UB.**

**Фикс:** либо отписываться в `~Application`, либо `weak_ptr`-схема.

---

---

### CR-104. `RefreshView`: `LabelKey(l)` возвращает `std::string`

**Где:** `Application.cpp:224`
Уже в CR-093. Плюс: что делает `LabelKey`? Ищет по имени или по `Entry()`? Если по имени — там кириллица (BUG-A5, `TCollection_AsciiString`).

---

---

### CR-105. `RefreshView`: `myOcctView->Redraw()` в конце — синхронно

**Где:** `Application.cpp:241`
`RefreshView` вызывается часто (после каждой операции). `Redraw()` — блокирующий. **Фикс:** отложить до следующего кадра, вызывать в `Run()`.

---

---

### CR-112. `OnSave` не обновляет `myProjectPaths[myActiveDocIndex]`
**Включает (merged):** CR-113, CR-115

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

---

### CR-119. `Standard_False` в `OnExportSTL`

**Где:** `Application.cpp:441`
```
writer.ASCIIMode() = Standard_False;
```

TECH-DEBT-005. **Фикс:** `false`.

**Оценка:** 1 минута.

---

---

### CR-126. `CloseDoc` — активным становится предыдущий, а не следующий

**Где:** `Application.cpp:355-357`
```
if (myActiveDocIndex >= (int)myDocuments.size()) {
    myActiveDocIndex = (int)myDocuments.size() - 1;   // последний
}
```

Если закрыли документ в **середине** списка (не активный) — активный не меняется. Если закрыли **активный в середине** — активным становится следующий, что логично (index сдвигается сам). Если закрыли **последний** — предыдущий.

Но: если закрыли активный в середине, по логике пользователя скорее следующий. Проверить на реальном сценарии.

**Оценка:** тест + 5 минут.

---

---

### CR-133. `std::cout` в sketch-логике — hot-path
**Включает (merged):** CR-097, CR-174, CR-244, CR-621, CR-744

**Где:** `Application.cpp:712, 727, 737, 748, 754, 764, 766, 771, 775, 778` (и в `Run()` тоже)

На **каждый клик** в эскизе — 5-7 вызовов `std::cout << ...`. На слабом железе (ARM, iGPU) `std::cout` **синхронный**, каждый flush — syscall.

**Дополнительно:** BUG-A4 — кириллица, но здесь ASCII, поэтому не проявляется.

**Фикс:**
1. Убрать `std::cout` из production-кода (заменить на `TRACE`).
2. Или `#ifdef BREEZECAD_DEBUG` — только в debug-сборке.

**Оценка:** 20 минут.

---

---

### CR-146. `try/catch` вокруг `AddSketchLine` — состояние не сбрасывается

**Где:** `Application.cpp:756-783`

```
try {
    myDocument->AddSketchLine(...);
    ...
    if (myAutoContinue && !loopClosed) {
        mySketchStartPoint = p;
        myHasSketchStart = true;   // ← если исключение — не сбросится
    }
} catch (const std::exception& e) {
    std::cerr << "Sketch: exception creating line: " << e.what() << "\n";
} catch (...) { ... }
```

Если `AddSketchLine` упадёт **после** частичного изменения:
- `myHasSketchStart` может остаться `true`.
- `mySketchStartPoint` — указывает на **мертвую** точку.

Следующий клик — попытка создать линию из ничего.

**Фикс:** `myHasSketchStart = false;` в catch.

**Оценка:** 5 минут.

---

---

### CR-149. `EnterSketch` — `std::cerr` вместо toast

**Где:** `Application.cpp:802, 808`

RULES п.26: «Уважение ко времени пользователя». Ошибка в **stderr** — не видна в GUI. Пользователь не понимает почему «не работает».

**Фикс:** `UI::Toast::Error("Не удалось войти в эскиз: родитель не является плоскостью")`.

**Оценка:** 20 минут (нужен toast-модуль).

---

---

### CR-150. `IsPointOnExistingVertex` — `threshold` не применяется к `skip`

**Где:** `Application.cpp:600-628`

```
if (dist2(p, origin) < th2 && dist2(skip, origin) > th2)
    return true;
```

Логика: точка `p` на Origin И `skip` **не** на Origin. Но:
- Если `skip` **внутри** threshold от Origin (только начали линию из Origin), то замыкание на Origin не срабатывает.
- Правильно ли это? Пользователь начал линию от Origin, вернулся к Origin — должен замкнуться **в Origin**, а не в другую точку.

**Похоже, инвертировано.** Проверить сценарий:
1. Первая линия: (0,0) → (10,0).
2. Вторая линия: (10,0) → (0,0). Ожидаем замыкание.
3. Но `skip = (10,0)`, `p = (0,0)`. `dist2(p, origin) = 0`, `dist2(skip, origin) = 100 > th2` — OK, замкнётся.

4. Другой сценарий: начали линию в Origin `skip = (0,0)`, ведём в Origin `p = (0,0)` — не сработает из-за `skip`.

Логика корректна, но **хрупкая**. Комментарий в коде есть, но понять сложно.

**Фикс:** документировать, но оставить.

---

---

### CR-152. `IsPointOnExistingVertex` — итерация без `it.More()` guard

**Где:** `Application.cpp:602`

```
TDF_ChildIterator it(sketchLabel, false);
for (; it.More(); it.Next()) { ... }
```

Если `sketchLabel.IsNull()` — падение? `TDF_ChildIterator` с Null label — UB. Guard: `if (sketchLabel.IsNull()) return false;` в начале.

**Фикс:** добавить guard.

**Оценка:** 2 минуты.

---

---

### CR-153. `OnSketchClick` — 130+ строк в одной функции, 7 ответственностей

**Где:** `Application.cpp:682-787`

**Разложение:**
1. Dependency dialog handling (~30 строк).
2. Snap logic (shift/points/grid, ~30 строк).
3. Loop check (~10 строк).
4. Line creation (~20 строк).
5. Auto-continue logic (~15 строк).
6. Extensive logging (~15 строк).

**Фикс:** разбить на:
- `HandleSketchDependencyClick(px, py)`.
- `ComputeSketchPoint(px, py) -> gp_Pnt` (snap+ortho).
- `CreateSketchLine(p)`.
- `ContinueOrFinishSketch(p, loopClosed)`.

**Оценка:** 1.5 часа (с тестами).

---

---

### CR-154. `myAutoContinue && !loopClosed` — если `myAutoContinue = false`, точки игнорируют loop

**Где:** `Application.cpp:770`

Если автопродолжение выключено, но петля замкнулась — что? Сбрасывается. OK.

Но если `myAutoContinue = true` и линия **не** замкнулась — продолжаем. Пользователь не может **остановить** рисование линий без Esc. Проверить UX.

**Оценка:** обсудить.

---

---

### CR-155. `OnSketchClick` — `myDependencyState.open` без проверки на `IsNull`

**Где:** `Application.cpp:685`

Если `myDependencyState` валиден, но `base`/`dependent` содержат **мёртвые** TDF_Label (после закрытия документа) — при `inList` пойдёт сравнение. `IsEqual` может вернуть `false` для мёртвых — ок. Но семантически «фича удалена, а ссылка висит».

**Фикс:** при `SwitchToDoc` / `CloseDoc` — очищать `myDependencyState`.

**Оценка:** 10 минут.

---

---

### CR-156. `std::strcpy(myLengthBuffer, "0")` — `strcpy` вместо `strncpy`

**Где:** `Application.cpp:745-746`

```
std::strcpy(myLengthBuffer, "0");
std::strcpy(myAngleBuffer, "0");
```

`strcpy` unsafe. Здесь безопасно (литерал в 1 символ, буфер 32), но паттерн плохой.

**Фикс:** `myLengthBuffer[0] = '0'; myLengthBuffer[1] = '\0';` или `strncpy`.

**Оценка:** 2 минуты.

---

---

### CR-157. `OnSketchClick` — magic `3.0` порог snap-to-point

**Где:** `Application.cpp:723`

```
if (SnapToKnownPoint(p, myActiveSketchLabel, myDocument->GetDoc(), 3.0, snapP)) {
```

`3.0` мм — очень **большой** порог. На экране 1000 px за 500 мм — 3 мм = 6 пикселей. Точка «прилипнет» в радиусе 6 px. Может мешать при рисовании близких точек.

**Фикс:** `constexpr double kSnapRadiusMM = 3.0;` + возможно брать из настроек.

**Оценка:** 5 минут.

---

---

### CR-158. `OnSketchClick` — magic `0.5` порог замыкания

**Где:** `Application.cpp:748`

```
IsPointOnExistingVertex(p, myActiveSketchLabel, mySketchStartPoint, 0.5)
```

`0.5` мм. Аналогично — вынести в константу.

**Оценка:** 3 минуты.

---

---

### CR-159. `OnSketchClick` — magic `1e-4` для совпадения точек

**Где:** `Application.cpp:738`

```
if (dist2 < 1e-4) {
```

`1e-4` = `(0.01 мм)²`. Почему 0.01? Документировать.

**Оценка:** 3 минуты.

---

---

### CR-160. `OnSketchClick` — `gp_Pnt p` — копируется при snap

**Где:** `Application.cpp:717, 726, 731`

```
gp_Pnt p;
if (!myOcctView->ScreenToPlane(px, py, p)) return;
...
if (mySnapToPoints) {
    gp_Pnt snapP;
    if (SnapToKnownPoint(p, ..., snapP)) {
        p = snapP;   // ← копирование
```

`gp_Pnt` — 3 double, копия = 24 байта. Мелочь, но в hot-path можно.

**Оценка:** WONTFIX (микро).

---

---

### CR-166. `ApplyExactLine` дублирует `OnSketchClick` — 33 строки копипасты

**Где:** `Application.cpp:883-916` vs `Application.cpp:756-783`

**Что дублируется:**
1. `myDocument->AddSketchLine(...)`.
2. `myOcctView->ClearPreviewLine()`.
3. `BRepBuilderAPI_MakeEdge(...)` — создание второго edge (CR-142).
4. maxTag-цикл + `FindChild` (CR-147).
5. `DisplaySketchLine(...)`.
6. `IsPointOnExistingVertex(...)` — loop check.
7. `myAutoContinue && !loopClosed` — auto-continue.
8. `myHasSketchStart = false` / `= true`.
9. `try/catch(...)` без сброса состояния (CR-146).

**Проблемы:**
- **Один баг = два фикса.**
- Логика уже рассинхронизирована: в `OnSketchClick` есть `myOcctView->ShowCursorMarker(p)`, в `ApplyExactLine` — тоже, но нет `myOcctView->ClearPreviewLine()` в else-ветке.
- Комментарии в `OnSketchClick` богаче — в `ApplyExactLine` их нет.

**Фикс:** вынести `CreateSketchLineInternal(startP, endP, style) -> bool` — одна реализация.

**Оценка:** 1 час (с тестами).

---

---

### CR-173. `ApplyExactLine` — `std::atof` без валидации

**Где:** `Application.cpp:872-873`

```
double length = std::atof(myLengthBuffer);
double angle  = std::atof(myAngleBuffer);
```

`atof` возвращает `0.0` при **любой** ошибке парсинга, не отличая «0» от «abc»:
- `"abc"` → `0.0` → `length < 1e-6` → выход.
- `"0"` → `0.0` → выход (правильно).
- `"1e999"` → `inf` → `length` = inf, идёт дальше. **UB при создании точки.**

**Фикс:** `std::strtod` с проверкой `errno == ERANGE` и `endptr`, или `std::from_chars`.

**Оценка:** 10 минут.

---

---

### CR-193. `RequestDelete` — `int childCount` может переполниться

**Где:** `Application.cpp:1015-1017`
```
int childCount = 0;
TDF_ChildIterator it(label, false);
for (; it.More(); it.Next()) childCount++;
```

Если у фичи 2^31 потомков — UB. В реальности невозможно, но паттерн плохой. **Фикс:** `size_t` + `%zu`.

---

---

### CR-194. `RequestDelete` — `TDF_ChildIterator` считает **прямых** детей, а не **всех**

**Где:** `Application.cpp:1016`

Комментарий в `snprintf`: «Будут удалены **все** вложенные элементы». Но `TDF_ChildIterator(label, false)` — **только прямые дети** (`allLevels = false`). Если у фичи есть внуки, сообщение врёт.

**Фикс:** либо считать рекурсивно (`TDF_ChildIterator(label, true)`), либо поменять текст «прямых потомков».

---

---

### CR-332. `ThemeManager` — магические числа 0/1/2/3

**Где:** `ThemeManager.h:15`

```
// theme: 0 = Светлая, 1 = Тёмная, 2 = Авто, 3 = legacy Dark.
void ApplyTheme(int theme, ThemeTokens& outTokens, OcctView* view);
```

**Фикс:** `enum class Theme : uint8_t { Light = 0, Dark = 1, System = 2, LegacyDark = 3 };`

Плюс — в `Application.h` `myCurrentTheme` → `Theme`.

**Связано с CR-089** (тот же magic в Application).

**Оценка:** 30 минут.

---

---

