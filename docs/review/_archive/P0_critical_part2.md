# P0 — КРИТИЧНО — часть 2 из 5

**Записей в части:** 40

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

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



### CR-133. `std::cout` в sketch-логике — hot-path

**Где:** `Application.cpp:712, 727, 737, 748, 754, 764, 766, 771, 775, 778` (и в `Run()` тоже)

На **каждый клик** в эскизе — 5-7 вызовов `std::cout << ...`. На слабом железе (ARM, iGPU) `std::cout` **синхронный**, каждый flush — syscall.

**Дополнительно:** BUG-A4 — кириллица, но здесь ASCII, поэтому не проявляется.

**Фикс:**
1. Убрать `std::cout` из production-кода (заменить на `TRACE`).
2. Или `#ifdef BREEZECAD_DEBUG` — только в debug-сборке.

**Оценка:** 20 минут.

---


### CR-137. `SnapToKnownPoint` — `std::vector<gp_Pnt>` на каждый вызов

**Где:** `Application.cpp:658`

```
std::vector<gp_Pnt> candidates;   // heap alloc, до 2*N entries
candidates.push_back(gp_Pnt(0, 0, 0));
...  // по 2 точки на каждую линию эскиза
```

Вызывается из `OnSketchClick` **на каждый клик**, а в `Run()` — **на каждый пиксель движения мыши** (>3px сдвиг).

При 100 линиях в эскизе — 200 heap-аллокаций на кадр движения. **Фикс:**
1. Два прохода: сначала find best, потом без вектора.
2. Или резервировать `vector` в `myCandidateBuffer` (member) и переиспользовать.

**Оценка:** 20 минут.

---


### CR-142. `OnSketchClick` — дубль создания edge

**Где:** `Application.cpp:757-760`

```
myDocument->AddSketchLine(myActiveSketchLabel, mySketchStartPoint, p, myCurrentLineStyle);
myOcctView->ClearPreviewLine();

TopoDS_Shape line = BRepBuilderAPI_MakeEdge(mySketchStartPoint, p).Shape();  // ← ЗАЧЕМ?
```

`AddSketchLine` уже создал edge и сохранил в OCAF. Здесь создаётся **второй, независимый** edge **только** для `DisplaySketchLine`. OCCT кидает его в heap.

**Фикс:** `BreezeDocument::AddSketchLine` **должен возвращать** `TopoDS_Shape` (или `TDF_Label`), и `DisplaySketchLine` использует тот же shape.

**Оценка:** 30 минут (менять сигнатуру).

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


### CR-147. `maxTag`-цикл — O(N) на каждый клик

**Где:** `Application.cpp:762-766`

```
int maxTag = 0;
TDF_ChildIterator it(myActiveSketchLabel, false);
for (; it.More(); it.Next()) {
    if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
}
TDF_Label lineLabel = myActiveSketchLabel.FindChild(maxTag, false);
```

**Проблемы:**
1. O(N) на каждый клик — при 1000 линиях в эскизе тормозит.
2. `FindChild(maxTag, false)` — флаг `false` = не создавать если нет. Но maxTag **только что найден** — должен существовать. А если `AddSketchLine` создал label с **другим** tag (нативный счётчик OCAF) — `FindChild` вернёт Null.
3. `myOcctView->DisplaySketchLine(line, lineLabel, ...)` — если `lineLabel.IsNull()`, что произойдёт?

**Фикс:** `AddSketchLine` возвращает созданный label. Убрать весь блок.

**Оценка:** входит в CR-142.

---


### CR-148. `EnterSketch` — `planeShape.ShapeType() != TopAbs_FACE` без fallback

**Где:** `Application.cpp:807-810`

```
TopoDS_Shape planeShape = myDocument->GetShape(parentPlane);
if (planeShape.IsNull() || planeShape.ShapeType() != TopAbs_FACE) {
    std::cerr << "EnterSketch: parent is not a face\n";
    return;
}
```

Если плоскость хранится как `COMPOUND` (частая практика в OCCT) — отказ. Пользователь жмёт «Начать эскиз» — ничего не происходит, ни сообщения, ни toast.

**Фикс:** fallback через `TopExp_Explorer(planeShape, TopAbs_FACE)` — взять первую грань.

**Оценка:** 15 минут.

---


### CR-149. `EnterSketch` — `std::cerr` вместо toast

**Где:** `Application.cpp:802, 808`

RULES п.26: «Уважение ко времени пользователя». Ошибка в **stderr** — не видна в GUI. Пользователь не понимает почему «не работает».

**Фикс:** `UI::Toast::Error("Не удалось войти в эскиз: родитель не является плоскостью")`.

**Оценка:** 20 минут (нужен toast-модуль).

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


### CR-151. `IsPointOnExistingVertex` — дублирование с `SnapToKnownPoint`

Обе функции **ищут существующие точки эскиза** — Origin + концы линий. Разница:
- `IsPointOnExistingVertex` — bool (есть/нет), с `skip`.
- `SnapToKnownPoint` — возвращает **ближайшую**.

Можно объединить: `FindNearestKnownPoint(p, sketch, skip, threshold, &out)`. Одна — O(N) с `skip`, вторая — O(N) без.

**Фикс:** одна функция.

**Оценка:** 30 минут.

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


### CR-154. `myAutoContinue && !loopClosed` — если `myAutoContinue = false`, точки игнорируют loop

**Где:** `Application.cpp:770`

Если автопродолжение выключено, но петля замкнулась — что? Сбрасывается. OK.

Но если `myAutoContinue = true` и линия **не** замкнулась — продолжаем. Пользователь не может **остановить** рисование линий без Esc. Проверить UX.

**Оценка:** обсудить.

---


### CR-155. `OnSketchClick` — `myDependencyState.open` без проверки на `IsNull`

**Где:** `Application.cpp:685`

Если `myDependencyState` валиден, но `base`/`dependent` содержат **мёртвые** TDF_Label (после закрытия документа) — при `inList` пойдёт сравнение. `IsEqual` может вернуть `false` для мёртвых — ок. Но семантически «фича удалена, а ссылка висит».

**Фикс:** при `SwitchToDoc` / `CloseDoc` — очищать `myDependencyState`.

**Оценка:** 10 минут.

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


### CR-157. `OnSketchClick` — magic `3.0` порог snap-to-point

**Где:** `Application.cpp:723`

```
if (SnapToKnownPoint(p, myActiveSketchLabel, myDocument->GetDoc(), 3.0, snapP)) {
```

`3.0` мм — очень **большой** порог. На экране 1000 px за 500 мм — 3 мм = 6 пикселей. Точка «прилипнет» в радиусе 6 px. Может мешать при рисовании близких точек.

**Фикс:** `constexpr double kSnapRadiusMM = 3.0;` + возможно брать из настроек.

**Оценка:** 5 минут.

---


### CR-158. `OnSketchClick` — magic `0.5` порог замыкания

**Где:** `Application.cpp:748`

```
IsPointOnExistingVertex(p, myActiveSketchLabel, mySketchStartPoint, 0.5)
```

`0.5` мм. Аналогично — вынести в константу.

**Оценка:** 3 минуты.

---


### CR-159. `OnSketchClick` — magic `1e-4` для совпадения точек

**Где:** `Application.cpp:738`

```
if (dist2 < 1e-4) {
```

`1e-4` = `(0.01 мм)²`. Почему 0.01? Документировать.

**Оценка:** 3 минуты.

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


### CR-167. `DrawPreviewInfo` — 7 хардкод-строк на английском

**Где:** `Application.cpp:934, 939, 953, 956, 971, 975, 978, 979`

```
ImGui::TextColored(..., "Length: %.2f mm", myPreviewLength);    // ← "Length:"
ImGui::Text("Exact input:");                                     // ← "Exact input:"
ImGui::InputText("Length", ...);                                 // ← "Length"
ImGui::InputText("Angle", ...);                                  // ← "Angle"
ImGui::Button("Apply", ...);                                     // ← "Apply"
ImGui::Button("Cancel", ...);                                    // ← "Cancel"
ImGui::TextDisabled("Tab - focus Length");                       // ← англ.
ImGui::TextDisabled("Enter - apply");                            // ← англ.
```

Локализация **полностью обойдена**. В русском UI пользователь видит «Exact input:» и «Tab - focus Length».

**Фикс:** `Tr("preview.length")`, `Tr("preview.exact_input")`, `Tr("preview.tab_hint")`, `Tr("preview.enter_hint")`, `Tr("btn.apply")`, `Tr("btn.cancel")` и т.д.

**Оценка:** 20 минут + 8 новых ключей в `Localization.cpp`.

---


### CR-168. `DrawPreviewInfo` — хардкод цветов (RULES п.30)

**Где:** `Application.cpp:934-935`

```
ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), ...);   // жёлтый
ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), ...);   // голубой
```

Прямое нарушение RULES п.30. Ни в светлой, ни в тёмной теме эти цвета не согласованы с палитрой.

**Фикс:** `myTheme.tokens.previewLengthColor`, `.previewAngleColor` или взять из существующих токенов (warning, info).

**Оценка:** 10 минут.

---


### CR-169. `DrawPreviewInfo` — `static bool focusLength` в member-функции

**Где:** `Application.cpp:944`

```
static bool focusLength = false;   // ← RULES п.20
```

**Это B-04 в REFACTOR_NOTES.** Сейчас функционально работает (одно окно preview), но если появится второй preview или функция вынесется в модуль — сломается.

**Фикс:** member-поле `myFocusLengthInput`.

**Оценка:** 3 минуты.

---


### CR-170. `DrawPreviewInfo` — магические числа позиции/размера окна

**Где:** `Application.cpp:925, 927`

```
ImVec2 pos = ImVec2(io.DisplaySize.x - 280, 60);   // ← 280, 60
ImGui::SetNextWindowSize(ImVec2(260, 0), ...);      // ← 260
```

**Фикс:** `constexpr float kPreviewMarginRight = 280.0f;` и т.д.

**Оценка:** 5 минут.

---


### CR-171. `DrawPreviewInfo` — `myOcctView->ClearPreviewLine()` без null check

**Где:** `Application.cpp:974`

```
if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    myHasSketchStart = false;
    myOcctView->ClearPreviewLine();   // ← если myOcctView = nullptr → краш
}
```

В начале функции проверяется `myActiveSketchLabel` и `myHasSketchStart`, но **не** `myOcctView`. Учитывая, что функция может вызываться в разных состояниях (после краха Init, например), — потенциальный краш.

**Фикс:** `if (myOcctView) myOcctView->ClearPreviewLine();`

**Оценка:** 1 минута.

---


### CR-172. `EnterSketch`/`ExitSketch` — magic `activeTab = 0/1`

**Где:** `Application.cpp:843, 860`

```
myRibbonState.activeTab = 1;   // Sketch tab
...
myRibbonState.activeTab = 0;   // 3D Model tab
```

`0` и `1` — magic. Связано с **CR-019** (enum class Tab).

**Фикс:** `myRibbonState.activeTab = static_cast<int>(Tab::Sketch);`.

**Оценка:** входит в CR-019.

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


### CR-174. `EnterSketch` / `ExitSketch` / `ApplyExactLine` — `std::cout` в hot-path

**Где:** `Application.cpp:848, 865, 879, 907, 909`

```
std::cout << "Application: entered sketch mode\n";
std::cout << "Application: exited sketch mode\n";
std::cout << "ApplyExactLine: length too small\n";
std::cout << "ApplyExactLine: line added, continuing\n";
std::cout << "ApplyExactLine: loop closed\n";
```

Связано с CR-133. `ExitSketch` вызывается на **каждый Esc**, `ApplyExactLine` — на **каждый точный ввод**.

**Фикс:** все `std::cout` → `TRACE(...)` (no-op по умолчанию).

**Оценка:** входит в CR-133.

---


### CR-175. `DrawViewCube` — aggregate init `Context{...}` хрупкий

**Где:** `Application.cpp:987-991`

```
UI::ViewCube::ViewCubeOverlay::Context ctx{
    *myOcctView,
    myTheme,
    ImGui::GetIO(),
    myRightPanelExpanded ? 110.0f : 55.0f
};
```

Если в `Context` добавят **новое поле в середину** (или изменят порядок) — этот aggregate init **скомпилируется**, но заполнит поля **неправильно**. Молча.

**Фикс:** named-field init (designated initializers C++20) или конструктор с явными параметрами.

**Оценка:** 15 минут.

---


### CR-176. `RequestDelete` — код обрезан, нужен полный

**Где:** `Application.cpp:995-1030`

Из видимого фрагмента (6 строк):
```
void Application::RequestDelete(const TDF_Label& label) {
    if (label.IsNull()) return;

    if (myDeleteConfirmState.skipConfirm) {
        // Удаляем сразу
        if (myDocument) myDocument->RemoveFeature(label);
```

**Не хватает** остатка функции. Скинь полностью (до строки 1030), чтобы завершить аудит.

---



### CR-191. `RequestDelete` — `<unnamed>` не локализовано

**Где:** `Application.cpp:1010`
```
std::string name = "<unnamed>";
```

**Фикс:** `Tr("misc.unnamed")`.

---


### CR-192. `RequestDelete` — русский хардкод в `snprintf`

**Где:** `Application.cpp:1023, 1027`
```
"Удалить «%s»?\n\nВместе с ним будут удалены %d вложенных элементов."
"Удалить «%s»?"
```

Формат русский — хорошо. Но если формат шаблона меняется при локализации (порядок аргументов в EN другой), `snprintf` сломается. **Фикс:** `Tr` с параметрами или `{name}` шаблон.

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


### CR-194. `RequestDelete` — `TDF_ChildIterator` считает **прямых** детей, а не **всех**

**Где:** `Application.cpp:1016`

Комментарий в `snprintf`: «Будут удалены **все** вложенные элементы». Но `TDF_ChildIterator(label, false)` — **только прямые дети** (`allLevels = false`). Если у фичи есть внуки, сообщение врёт.

**Фикс:** либо считать рекурсивно (`TDF_ChildIterator(label, true)`), либо поменять текст «прямых потомков».

---


### CR-195. `RequestDelete` — `RefreshView()` + `FitAll()` дублирует паттерн

Связано с CR-127. Паттерн «после операции → RefreshView + FitAll» повторяется 6+ раз.

**Фикс:** `Application::AfterModelChange()`.

---


### CR-196. `CMakeLists.txt` — `TKCAF TKLCAF TKBin` всё ещё линкуются

**Где:** `CMakeLists.txt:135`
```
TKCAF TKLCAF TKBin
```

**По ADR-0001:** OCAF должен быть **модулем-мостом**, не в базе. Сейчас эти три DLL входят в поставку → лишний вес.

**Фикс:** исключить после перехода на свой `.bzc`. **Сейчас** — задокументировать в CODE_REVIEW как TECH-DEBT.

**Оценка:** после Фазы 3.

---


### CR-197. `CMakeLists.txt` — `CMAKE_CXX_FLAGS_RELEASE` перетирает дефолт

**Где:** `CMakeLists.txt:14-15`
```
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
```

**Проблема:** эти переменные CMake устанавливает **сам** со своими значениями. Перезапись через `set` **стирает** всё, что туда добавили бы внешние модули (`OpenCASCADE`, `glfw`).

**Фикс:**
```
target_compile_options(breezecad PRIVATE
    $<$<CONFIG:Release>:-O2>
)
```

**Оценка:** 20 минут.

---


### CR-198. `CMakeLists.txt` — нет `-Wall -Wextra -Wpedantic`

**Где:** весь файл
Связано с CR-023. **Фикс:**
```
target_compile_options(breezecad PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unused-parameter>  # или включать всё
)
```

**Оценка:** 30 минут + правки существующих warnings.

---


### CR-199. `CMakeLists.txt` — `include_directories` глобальный, deprecated

**Где:** `CMakeLists.txt:21, 33`
```
include_directories(${LUNASVG_ROOT}/include)
...
include_directories(${OpenCASCADE_INCLUDE_DIR})
```

Глобальный `include_directories` устарел. **Фикс:** `target_include_directories(... SYSTEM PRIVATE ...)`.

**Оценка:** 10 минут.

---


### CR-200. `CMakeLists.txt` — нет `-flto` для Release

**Цель проекта:** вес 200-500 МБ. Без LTO — не достижимо. `-flto` режет размер exe на 10-30%.

**Фикс:**
```
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok)
if(ipo_ok)
    set_property(TARGET breezecad PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
```

**Оценка:** 15 минут + проверка линковки с OCCT.

---


