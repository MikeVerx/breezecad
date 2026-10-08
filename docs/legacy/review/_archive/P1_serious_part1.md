# P1 — СЕРЬЁЗНО — часть 1 из 2

**Записей в части:** 57

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-005. `Button` — 11 параметров, boolean blindness

**Где:** `RibbonButton.h`
**Статус:** OPEN

**Проблема:**
```
int Button(IconManager& icons,
           const char* id, const char* const* opts, int optCount, int& idx,
           const char* icon, bool enabled, bool big,
           const char* tooltip = nullptr, float iconScale = 1.0f);
```

Вызов:
```
Button(icons, "nav_pan", panOpts, 1, dummy, ICON_PAN, true, true, "Панорамирование");
```
Глядя на `true, true` — невозможно понять, что это `enabled, big`.

**Фикс:** `struct ButtonOpts` с named-полями. `Button(icons, opts)`.

**Оценка:** 1 час.

---


### CR-006. `int` возврат (0/1/2/3) — magic numbers

**Где:** `RibbonButton.h`
**Статус:** OPEN

**Проблема:**
```
int r = Button(...);
if (r == 1 || r == 3) { ... }
```
Что такое 1 и 3? Через месяц никто не вспомнит.

**Фикс:** `enum class BtnResult : uint8_t { None, Main, Arrow, Menu };` + `[[nodiscard]]`.

**Оценка:** 30 минут + правки call-sites.

---


### CR-007. `ctx.drawTabs` — мёртвое поле

**Где:** `RibbonContext.h:29`
**Статус:** OPEN

```
std::function<void()> drawTabs;   // никогда не вызывается
```

`Ribbon.cpp` зовёт `UI::Ribbon::DrawTabs(*ctx.icons, *ctx.state)` напрямую.

**Фикс:** удалить поле.

**Оценка:** 1 минута.

---


### CR-008. nullptr-проверки неконсистентны

**Где:** `Ribbon.cpp:34, 46, 50`
**Статус:** OPEN

**Проблема:**
```
if (ctx.icons && ctx.state) UI::Ribbon::DrawTabs(...);   // проверка есть
...
switch (ctx.state->activeTab) {                           // проверки нет
    case 0: UI::Ribbon::DrawModelTab(*ctx.icons, ...);   // и тут нет
```

**Фикс:** определиться с контрактом:
- **A.** «`ctx.state` всегда не-null» — `assert` в debug, проверки убрать.
- **B.** «может быть null» — проверять везде.

Рекомендую **A**.

**Оценка:** 10 минут.

---


### CR-009. `bool* hasSketchStart` — control coupling

**Где:** `SketchTab.cpp:77`
**Статус:** OPEN

**Проблема:**
```
if (r == 1 || r == 3) {
    if (ctx.hasSketchStart) *ctx.hasSketchStart = false;   // UI мутирует Application
}
```
UI не должен знать о внутренностях Application.

**Фикс:** колбэк `onLineToolSelected()` — Application сам решает что сбрасывать.

**Оценка:** 15 минут.

---


### CR-010. `splitMiddle` создаёт `std::vector<size_t>` каждый вызов

**Где:** `RibbonButton.cpp:~118`
**Статус:** OPEN

**Проблема:**
```
auto splitMiddle = [&](const std::string& s) {
    std::vector<size_t> spaces;   // heap alloc
    ...
};
```
Вызывается на каждой кнопке × каждый кадр × каждый `fs` в цикле.

**Фикс:** найти средний пробел без вектора — два прохода.
**Занести как OPT-05 в REFACTOR_NOTES.**

**Оценка:** 15 минут.

---


### CR-011. Неконсистентный стиль групп вкладок

**Где:**
- `ModelTab.cpp`, `SketchTab.cpp`, `ViewTab.cpp`, `AssemblyTab.cpp` — `GroupStart/GroupEnd`
- `AnnotateTab.cpp`, `ToolsTab.cpp`, `ManageTab.cpp` — `BeginGroup/TextColored/SeparatorEx`

**Статус:** OPEN

4 «новые» вкладки используют Inventor-style, 3 «старые» — плоские. Причина: их я перенёс **как было** из Application, они уже были уродливые.

**Фикс:** привести 3 старых к стилю `GroupStart/GroupEnd` + `Tr("grp.*")`.

**Оценка:** 1.5 часа (переписать 3 вкладки).

---


### CR-012. `ImGui::SeparatorEx` + `ImGuiSeparatorFlags_Vertical` — internal API

**Где:** `AnnotateTab.cpp`, `ToolsTab.cpp`, `ManageTab.cpp`
**Статус:** OPEN

`SeparatorEx` — **не** public ImGui API. Может исчезнуть или измениться.

**Фикс:** исчезнет после CR-011.

---


### CR-013. `static const char* oneOpt[] = { "tool.dummy" };` — мёртвая переменная

**Где:** `ModelTab.cpp:19`, `AnnotateTab.cpp`, `ToolsTab.cpp`, `ManageTab.cpp`, `SketchTab.cpp`
**Статус:** OPEN

Объявлена, нигде не используется.

**Фикс:** удалить.

**Оценка:** 5 минут.

---


### CR-014. `if (!done) drawBottom({label}, baseSize * 0.45f);` — магия

**Где:** `RibbonButton.cpp`
**Статус:** OPEN

Почему 0.45? При базовом 16 → 7.2 px. При 32 → 14.4. Нечитаемо.

**Фикс:** `constexpr float kFallbackFontScale = 0.45f;` + комментарий почему.

**Оценка:** 5 минут.

---


### CR-015. `ImVec2 startPos = ImGui::GetCursorScreenPos();` — неиспользуемая

**Где:** `RibbonButton.cpp:54`
**Статус:** OPEN

Ни разу не используется. GCC с `-Wall` должен дать warning.

**Фикс:** удалить.

**Оценка:** 1 минута.

---


### CR-016. `Application.cpp:534` — устаревший комментарий

**Где:** `Application.cpp:534`
**Статус:** OPEN

```
// Универсальная кнопка Ribbon с SVG-иконкой Lucide.
void Application::DrawRibbon() {   // ← уже не кнопка, а оркестратор
```

**Фикс:** убрать или актуализировать.

**Оценка:** 1 минута.

---



### CR-032. `RibbonButton.cpp` — `inArrow` проверяет только `X`, не `Y`

**Где:** `RibbonButton.cpp:65`

```
bool inArrow = hasArrow && (mp.x >= min.x + w);
```

Если курсор **ниже** кнопки, но по X в зоне стрелки → `inArrow = true`. Hover подсветит стрелку, хотя мышь вне кнопки.

**Фикс:** добавить `mp.y >= min.y && mp.y <= max.y`.

---


### CR-033. `RibbonButton.cpp` — fallback-иконка — хардкод цвета

**Где:** `RibbonButton.cpp:105`

```
dl->AddRectFilled(p1, ..., IM_COL32(140, 140, 140, 160), 4.0f);
```

RULES п.30. В `ThemeTokens` — нет такого токена.

---


### CR-034. `RibbonButton.cpp` — магические множители шрифта

**Где:** `RibbonButton.cpp:148`

```
float sizes[] = { baseSize, baseSize * 0.82f, baseSize * 0.62f, baseSize * 0.52f };
```

**Фикс:** `constexpr float kFontScaleSteps[] = {1.0f, 0.82f, 0.62f, 0.52f};`.

---


### CR-035. `RibbonButton.cpp` — silent fail на `chevron-down`

**Где:** `RibbonButton.cpp:165`

```
GLuint arrowTex = icons.Get("chevron-down", 10);
if (arrowTex != 0) { ... }   // если нет — молча ничего
```

**Фикс:** debug-assert + warning в `IconManager` при NOT FOUND.

---


### CR-036. `RibbonButton.cpp` — `Tr(opts[i]).c_str()` — потенциальный dangling

**Где:** `RibbonButton.cpp:178`

Проверить `Localization.h`: `Tr` возвращает `const std::string&` или `std::string`? Если второе — `c_str()` от временного объекта = UB.

---


### CR-037. `RibbonGroup.cpp` — `TARGET_H = 76.0f`, а UI_LAYOUT говорит 78

**Где:** `RibbonGroup.cpp:13`
**Канон:** UI_LAYOUT.md §2.2 — Ribbon 78 px.
**Фикс:** согласовать. Возможно, 76 = высота без бордюра.

---


### CR-038. `RibbonGroup.cpp` — magic offsets

**Где:** `RibbonGroup.cpp:22-25`

```
dl->AddRectFilled(
    ImVec2(pMin.x - 6, pMin.y - 4),
    ImVec2(pMax.x + 6, pMax.y + 18),
    bg, 4.0f);
```

`-6, -4, +6, +18, 4.0f` — 5 magic.

---


### CR-039. `RibbonGroup.cpp` — хардкод цветов

**Где:** `RibbonGroup.cpp:20, 33, 38-40`

```
bg = isLight ? IM_COL32(255, 255, 255, 60) : IM_COL32(255, 255, 255, 12);
isLight ? IM_COL32(160, 160, 160, 140) : IM_COL32(90, 100, 115, 180);
isLight ? IM_COL32(90, 90, 90, 230) : IM_COL32(180, 185, 195, 220);
```

RULES п.30.

---


### CR-040. `RibbonGroup.cpp` — `SameLine(0, 22)` magic

**Где:** `RibbonGroup.cpp:47`

---


### CR-041. `RibbonTabs.cpp` — `isLight` считается в цикле

**Где:** `RibbonTabs.cpp:22`
Вынести до цикла.

---


### CR-042. `RibbonTabs.cpp` — хардкод цветов вкладок

**Где:** `RibbonTabs.cpp:25-30`
`ImVec4(0.698f, 0.714f, 0.737f, 1.0f)` и `ImVec4(0.20f, 0.55f, 0.95f, 1.0f)` — RULES п.30.

---


### CR-043. `RibbonTabs.cpp` — magic числа SvgIconBtn

**Где:** `RibbonTabs.cpp:41`
`SvgIconBtn(icons, "ribbon_plus", "plus", 16, 32, 26, ...)` — `16, 32, 26`.

---


### CR-044. `RibbonTabs.cpp` — `SetTooltip("Подключить модуль")` без `Tr()`

**Где:** `RibbonTabs.cpp:45`
Локализация обойдена.

---


### CR-045. `SketchTab.cpp` — `static int projIdx = 0;`

**Где:** `SketchTab.cpp:36`
RULES п.20. Должно быть в `RibbonState`.

---


### CR-046. `SketchTab.cpp` — цвета стилей линий — хардкод

**Где:** `SketchTab.cpp:200-202`

```
{ "Основная",   "Основная линия",        LS_Normal,       IM_COL32(140, 51, 191, 255) },
{ "Вспомог.",   "Вспомогательная линия", LS_Construction, IM_COL32(115, 128, 166, 255) },
{ "Осевая",     "Осевая линия",          LS_Centerline,   IM_COL32(179, 102, 38, 255) },
```

RULES п.30. Цвета должны быть в `ThemeTokens`.

---


### CR-047. `SketchTab.cpp` — тихая деградация при `nullptr`

**Где:** `SketchTab.cpp:205, 219`

```
if (ctx.currentLineStyle) *ctx.currentLineStyle = items[i].style;
```

Если `nullptr` — подсветка работает, но выбор не сохраняется. Пользователь в недоумении. **Надо assert или лог.**

---


### CR-048. `SketchTab.cpp` — хардкод «неактивного» цвета

**Где:** `SketchTab.cpp:207`
`ImVec4(0.22f, 0.24f, 0.30f, 1.0f)` — RULES п.30.

---


### CR-054. `Annotate/Tools/Manage.cpp` — цвета текста не через тему

**Где:** все три файла
`ImGui::GetStyleColorVec4(ImGuiCol_Text)` — базовый ImGui, не токен проекта.

**Фикс:** после CR-011 автоматически уйдёт (там будет `GroupEnd` с `Tr`).

---



### CR-106. `ApplyTheme`: `.get()` из `unique_ptr` — можно было ссылку

**Где:** `Application.cpp:133`
```
UI::Theme::ApplyTheme(theme, myTheme, myOcctView.get());
```
Если функция принимает `OcctView*` — ок. Но если можно `OcctView&` — лучше ссылка.

---


### CR-107. `UpdateTelemetry`: логика под `#ifdef _WIN32` — нужен `#else` для Unix

**Где:** `Application.cpp:1078-1087`
Сейчас на Unix — RAM = 0.0. **Фикс:** `#elif __linux__` — `/proc/self/status`, `#elif __APPLE__` — `task_info`. Или `Platform::GetRamMB()`.

---


### CR-108. `RefreshView`: рекурсия без защиты от циклов

**Где:** `Application.cpp:220-238`
`walk` рекурсивно обходит дерево. Если OCAF-граф содержит цикл (не должен, но) — **stack overflow**.

**Фикс:** max-depth или visited-set.

---


### CR-109. `RefreshView`: `walk` не проверяет `l.IsNull()`

**Где:** `Application.cpp:220`
Первая итерация `walk(myDocument->MainLabel())` — если `MainLabel()` null, внутри `GetType(l)` — UB.

**Фикс:** `if (l.IsNull()) return;` в начале.

---


### CR-110. `~Application`: `myIcons` не освобождает GL-текстуры

**Где:** `Application.cpp:199-203`
`IconManager` держит `GLuint` текстуры. Деструктор их не удаляет. **Утечка GPU RAM** при каждом создании/уничтожении Application (например, в тестах).

**Связано с RAM-H1.**

---



### CR-120. `OnUndo` / `OnRedo` — двойной рендер

**Где:** `Application.cpp:469-528`

```
if (myDocument->Undo()) {
    RefreshView();                          // ← рендер 1
    if (!myActiveSketchLabel.IsNull()) {
        if (myOcctView) myOcctView->ClearAllShapes();
        RefreshSketchLines();                // ← рендер 2
        myOcctView->ShowGrid(myShowGrid);
    }
}
```

`RefreshView()` внутри уже вызывает `Redraw()`. Потом `RefreshSketchLines()` дёргает ещё. **Два рендера за кадр.**

**Фикс:** объединить. `RefreshView` должен знать о режиме эскиза.

**Оценка:** 15 минут.

---


### CR-121. `CreateNewDoc` — нет проверки на ошибку `CreateBaseGeometry`

**Где:** `Application.cpp:255`
```
doc->CreateBaseGeometry();   // ← если упадёт, идём дальше
myDocuments.push_back(std::move(doc));
```

Если `CreateBaseGeometry` вернёт ошибку (или бросит), документ всё равно пушится — полупустой.

**Фикс:** проверять возврат / ловить исключение.

---


### CR-125. `SwitchToDoc` — ручной сброс эскиза из 4 команд

**Где:** `Application.cpp:299-304, 348-353`

```
myActiveSketchLabel = TDF_Label();
myHasSketchStart = false;
if (myOcctView) {
    myOcctView->ClearPreviewLine();
    myOcctView->ClearAllShapes();
    myOcctView->ClearGrid();
    myOcctView->ExitSketchMode();
}
```

Дублируется в `SwitchToDoc` и `CloseDoc`. **Фикс:** `Application::ResetSketchState()` — helper.

---


### CR-123. `std::cout` / `std::cerr` — BUG-A4

**Где:** 15+ мест в этом блоке.
Кириллица → CP866-кракозябры в MSYS2.

**Фикс:** единый логгер или `setlocale`.

---



### CR-134. `RefreshSketchLines` — не проверяет `myActiveSketchLabel` на валидность после `SwitchToDoc`

**Где:** `Application.cpp:579`

Функция вызывается из `OnUndo`, `OnRedo`. Проверяет `IsNull()`, но не факт, что label ещё **жив** в документе (при закрытии документа label осиротел).

**Фикс:** дополнительная проверка или очистка при `SwitchToDoc`/`CloseDoc` (уже есть в CR-114).

---


### CR-135. `RefreshSketchLines` — O(N) обход, полный `ClearAllShapes` перед ним

**Где:** `Application.cpp:473, 520` — вызов `ClearAllShapes` + `RefreshSketchLines`.

При отмене действия — стирается **всё**, заново рисуется **всё**. Для сложного эскиза — перерисовка 1000 линий.

**Фикс:** инкрементальное обновление.

---


### CR-136. `RefreshSketchLines` — `static_cast<int>(style)` — сокрытие типа

**Где:** `Application.cpp:588`

```
myOcctView->DisplaySketchLine(s, lineLabel, static_cast<int>(style));
```

`LineStyle` → `int`. Зачем? `DisplaySketchLine` ожидает `int`? Проверить сигнатуру.

---


### CR-138. `SnapToKnownPoint` — `threshold * threshold` вычисляется 1 раз, ок, но `dx*dx+dy*dy+dz*dz` можно через `BRep_Tool::Pnt` или OCCT helper

Мелочь.

---


### CR-139. `SnapToKnownPoint` — `doc` параметр не используется для валидации `sketchLabel`

**Где:** `Application.cpp:642`

```
if (doc.IsNull() || sketchLabel.IsNull()) return false;
```

Проверяет оба — ок. Но `doc` далее нигде не используется. Убрать параметр?

---


### CR-140. `SnapToKnownPoint` — `bestDist` без нормализации на `threshold`

```
double bestDist = threshold * threshold;
```
Если `threshold = 3.0`, `bestDist = 9.0`. Если `threshold = 0.1`, `bestDist = 0.01`. Пороговое сравнение — ок, но неясно **почему** именно так.

**Фикс:** `const double thresholdSq = threshold * threshold;` — сразу к квадрату, комментарий.

---


### CR-141. `OnSketchClick` — `myDocument->GetDoc()` в hot-path

**Где:** `Application.cpp:723`

`GetDoc()` — возвращает `Handle`, refcount inc/dec на каждый вызов. **Фикс:** проверить сигнатуру, возможно `const Handle&`.

---


### CR-143. `OnSketchClick` — не проверяет `myDependencyState` на консистентность

Связано с CR-155.

---


### CR-144. `OnSketchClick` — `glfwGetKey` для Shift (CR-062 повтор)

**Где:** `Application.cpp:714-715`

```
bool shiftHeld = glfwGetKey(..., GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
              || glfwGetKey(..., GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
```

`io.KeyShift` — уже доступен. **Фикс:** `ImGui::GetIO().KeyShift`.

---


### CR-145. `OnSketchClick` — оба `glfwGetKey` вызываются всегда, без short-circuit

**Где:** `Application.cpp:714-715`

`||` короткое замыкание работает: если LEFT нажат, RIGHT не проверяется. Но если **не** нажат LEFT — вызов. Мелочь.

**Фикс:** `ImGui::GetIO().KeyShift` (одно поле).

---



### CR-177. `ExitSketch` — полный `RefreshView()` для переключения таба

**Где:** `Application.cpp:862`

```
RefreshView();
if (myOcctView) myOcctView->FitAll();
```

`ExitSketch` полностью перерисовывает всё дерево. Для 1000 фич — долго. **Фикс:** инкрементально вернуть только скрытые системные объекты.

---


### CR-178. `ExitSketch` — `FitAll()` каждый выход

**Где:** `Application.cpp:863`

Пользователь в эскизе **зумил** на нужный участок. Выходит из эскиза → камера сбрасывается в FitAll. **UX-баг.**

**Фикс:** сохранять камеру при входе в эскиз, восстанавливать при выходе.

---


### CR-179. `EnterSketch` — `RefreshSketchLines()` без null check

**Где:** `Application.cpp:846`

`RefreshSketchLines` внутри делает `if (!myDocument || !myOcctView || ...) return;`. Формально защищено. Но проверка происходит **внутри** — если кто-то изменит `RefreshSketchLines`, `EnterSketch` сломается неявно.

**Фикс:** проверка в `EnterSketch` до вызова.

---


### CR-180. `EnterSketch` — `Standard_Real u/v` — TECH-DEBT-005

**Где:** `Application.cpp:832-833`

```
Standard_Real u = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
Standard_Real v = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;
```

TECH-DEBT-005. **Фикс:** `double`.

---


### CR-181. `DrawPreviewInfo` — `AlwaysAutoResize` + `SetNextWindowSize` конфликтуют

**Где:** `Application.cpp:927, 931`

```
ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);   // фикс ширина
...
ImGuiWindowFlags_AlwaysAutoResize                              // авто размер
```

Два взаимоисключающих флага. Скорее всего работает, но непредсказуемо. **Фикс:** выбрать одно.

---


### CR-182. `DrawPreviewInfo` — `Tab` перехватывает фокус без проверки контекста

**Где:** `Application.cpp:944-947`

```
if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !ImGui::GetIO().WantTextInput) {
    focusLength = true;
}
```

`Tab` в ImGui используется для навигации. Перехват может сломать UI-навигацию в других виджетах. **Проверить.**

---



### CR-210. `RequestDelete` — `TDF_ChildIterator` в цикле без консистентности

**Где:** `Application.cpp:1016`

Во время обхода `label` может быть удалён другим потоком (если появится multithreaded доступ). Сейчас ок (однопоточно). Записать как FUTURE-риск.

---


### CR-211. `RequestDelete` — `Handle(TDataStd_Name)` без проверки типа

**Где:** `Application.cpp:1011`
```
if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
    name = ExtToUtf8(nameAttr->Get());
}
```

`nameAttr->Get()` возвращает `TCollection_ExtendedString`. `ExtToUtf8` — из `util/Utf8`. BUG-A5 может проявиться.

**Фикс:** проверить `ExtToUtf8` на кириллицу.

---


