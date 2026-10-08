# BreezeCAD — Review: P1 / app

**Записей:** 16

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-106. `ApplyTheme`: `.get()` из `unique_ptr` — можно было ссылку

**Где:** `Application.cpp:133`
```
UI::Theme::ApplyTheme(theme, myTheme, myOcctView.get());
```
Если функция принимает `OcctView*` — ок. Но если можно `OcctView&` — лучше ссылка.

---

---

### CR-107. `UpdateTelemetry`: логика под `#ifdef _WIN32` — нужен `#else` для Unix

**Где:** `Application.cpp:1078-1087`
Сейчас на Unix — RAM = 0.0. **Фикс:** `#elif __linux__` — `/proc/self/status`, `#elif __APPLE__` — `task_info`. Или `Platform::GetRamMB()`.

---

---

### CR-108. `RefreshView`: рекурсия без защиты от циклов

**Где:** `Application.cpp:220-238`
`walk` рекурсивно обходит дерево. Если OCAF-граф содержит цикл (не должен, но) — **stack overflow**.

**Фикс:** max-depth или visited-set.

---

---

### CR-109. `RefreshView`: `walk` не проверяет `l.IsNull()`

**Где:** `Application.cpp:220`
Первая итерация `walk(myDocument->MainLabel())` — если `MainLabel()` null, внутри `GetType(l)` — UB.

**Фикс:** `if (l.IsNull()) return;` в начале.

---

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

---

### CR-134. `RefreshSketchLines` — не проверяет `myActiveSketchLabel` на валидность после `SwitchToDoc`

**Где:** `Application.cpp:579`

Функция вызывается из `OnUndo`, `OnRedo`. Проверяет `IsNull()`, но не факт, что label ещё **жив** в документе (при закрытии документа label осиротел).

**Фикс:** дополнительная проверка или очистка при `SwitchToDoc`/`CloseDoc` (уже есть в CR-114).

---

---

### CR-135. `RefreshSketchLines` — O(N) обход, полный `ClearAllShapes` перед ним

**Где:** `Application.cpp:473, 520` — вызов `ClearAllShapes` + `RefreshSketchLines`.

При отмене действия — стирается **всё**, заново рисуется **всё**. Для сложного эскиза — перерисовка 1000 линий.

**Фикс:** инкрементальное обновление.

---

---

### CR-144. `OnSketchClick` — `glfwGetKey` для Shift (CR-062 повтор)

**Где:** `Application.cpp:714-715`

```
bool shiftHeld = glfwGetKey(..., GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
              || glfwGetKey(..., GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
```

`io.KeyShift` — уже доступен. **Фикс:** `ImGui::GetIO().KeyShift`.

---

---

### CR-145. `OnSketchClick` — оба `glfwGetKey` вызываются всегда, без short-circuit

**Где:** `Application.cpp:714-715`

`||` короткое замыкание работает: если LEFT нажат, RIGHT не проверяется. Но если **не** нажат LEFT — вызов. Мелочь.

**Фикс:** `ImGui::GetIO().KeyShift` (одно поле).

---

---

### CR-177. `ExitSketch` — полный `RefreshView()` для переключения таба

**Где:** `Application.cpp:862`

```
RefreshView();
if (myOcctView) myOcctView->FitAll();
```

`ExitSketch` полностью перерисовывает всё дерево. Для 1000 фич — долго. **Фикс:** инкрементально вернуть только скрытые системные объекты.

---

---

### CR-178. `ExitSketch` — `FitAll()` каждый выход

**Где:** `Application.cpp:863`

Пользователь в эскизе **зумил** на нужный участок. Выходит из эскиза → камера сбрасывается в FitAll. **UX-баг.**

**Фикс:** сохранять камеру при входе в эскиз, восстанавливать при выходе.

---

---

### CR-180. `EnterSketch` — `Standard_Real u/v` — TECH-DEBT-005

**Где:** `Application.cpp:832-833`

```
Standard_Real u = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
Standard_Real v = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;
```

TECH-DEBT-005. **Фикс:** `double`.

---

---

### CR-181. `DrawPreviewInfo` — `AlwaysAutoResize` + `SetNextWindowSize` конфликтуют
**Включает (merged):** CR-182, CR-187, CR-188, CR-189, CR-190

**Где:** `Application.cpp:927, 931`

```
ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);   // фикс ширина
...
ImGuiWindowFlags_AlwaysAutoResize                              // авто размер
```

Два взаимоисключающих флага. Скорее всего работает, но непредсказуемо. **Фикс:** выбрать одно.

---

---

### CR-210. `RequestDelete` — `TDF_ChildIterator` в цикле без консистентности

**Где:** `Application.cpp:1016`

Во время обхода `label` может быть удалён другим потоком (если появится multithreaded доступ). Сейчас ок (однопоточно). Записать как FUTURE-риск.

---

---

### CR-212. `RequestDelete` — `snprintf` не проверяет переполнение

**Где:** `Application.cpp:1023`

Если имя фичи очень длинное (>200 симв.), `snprintf` обрежет. Может обрезать посередине UTF-8 → кракозябры.

**Фикс:** проверка длины имени перед snprintf.

---

---

