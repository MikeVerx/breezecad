# BreezeCAD — Review: P1 / occt

**Записей:** 9

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-136. `RefreshSketchLines` — `static_cast<int>(style)` — сокрытие типа

**Где:** `Application.cpp:588`

```
myOcctView->DisplaySketchLine(s, lineLabel, static_cast<int>(style));
```

`LineStyle` → `int`. Зачем? `DisplaySketchLine` ожидает `int`? Проверить сигнатуру.

---

---

### CR-179. `EnterSketch` — `RefreshSketchLines()` без null check

**Где:** `Application.cpp:846`

`RefreshSketchLines` внутри делает `if (!myDocument || !myOcctView || ...) return;`. Формально защищено. Но проверка происходит **внутри** — если кто-то изменит `RefreshSketchLines`, `EnterSketch` сломается неявно.

**Фикс:** проверка в `EnterSketch` до вызова.

---

---

### CR-271. `OcctView.cpp` — инициализация `myGridVisible = true`, но сетка не построена

**Где:** `OcctView.h:143` + `OcctView.cpp`

`myGridVisible = true` по умолчанию. Но `BuildSketchGrid` вызывается только в `EnterSketchMode`. В 3D-режиме `myGridVisible = true` — а сетки нет. Если кто-то проверит флаг — получит **неверное состояние**.

**Фикс:** `myGridVisible = false` до первого `BuildSketchGrid`.

**Оценка:** 2 минуты.

---

---

### CR-274. `OcctView::Init` — `myWindow->SetVirtual(true)`

**Где:** `OcctView.cpp:104`

`SetVirtual(true)` — обычно для offscreen rendering. Для real-window — `false`. Проверить, не мешает ли.

**Оценка:** 10 минут.

---

---

### CR-275. `OcctView::Init` — `myWindow->IsMapped()` и `Map()` без проверки состояния

**Где:** `OcctView.cpp:134-135`

```
if (!myWindow->IsMapped()) myWindow->Map();
```

Идиоматично. ОК.

**WONTFIX.**

---

---

### CR-276. `OcctView` — `SetImmediateUpdate(false)` — что за ним

**Где:** `OcctView.cpp:99`

```
myView->SetImmediateUpdate(false);
```

Отключает немедленный redraw при изменениях. Нужно явно вызывать `Redraw()`. Это **правильно**, но проверь, что **все** точки изменения вызывают `Redraw()`.

**Оценка:** audit в части 2.

---

---

### CR-277. `OcctView.cpp` — `#include <AIS_Line.hxx>` в `.h` и `.cpp`

**Где:** `OcctView.h:5`, `OcctView.cpp:9`

`AIS_Line` используется в `myPreviewLine` (member). Точно нужен в `.h`. В `.cpp` — если уже приходит через header, лишний.

**Оценка:** 2 минуты.

---

---

### CR-278. `OcctView::DisplayShape` — `isPlane2` проверяется вторично

**Где:** `OcctView.cpp:213-221`

```
if (isSystem) {
    bool isPlane2 = (shape.ShapeType() == TopAbs_FACE);   // ← снова то же
```

В блоке выше (`isSystem`) уже было `isPlane = (shape.ShapeType() == TopAbs_FACE)`. Дублирование.

**Фикс:** переиспользовать `isPlane`.

**Оценка:** 5 минут.

---

---

### CR-280. `OcctView.cpp` — `std::cout` в `EnterSketchMode` / `ExitSketchMode`

**Где:** `OcctView.cpp:369` и другие

Связано с CR-133. **Фикс:** TRACE.

---

---

---

