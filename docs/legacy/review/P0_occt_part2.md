# BreezeCAD — Review: P0 / occt

**Записей:** 6
**Часть:** 2/2

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-316. `Resize` — не обновляет `myWidth`/`myHeight` перед проверкой

**Где:** `OcctView.cpp:681-687`

```
void OcctView::Resize(int width, int height) {
    myWidth  = width;
    myHeight = height;
    if (myWindow.IsNull() || myView.IsNull()) return;
    myWindow->SetSize(width, height);
    myView->MustBeResized();
}
```

Порядок: сначала обновляет поля, потом guard. Если `myWindow.IsNull()`, поля всё равно обновлены. Формально ок, но порядок не идиоматичен.

**WONTFIX** — работает.

---

---

### CR-317. `SelectAtPixel` и `OnMouseButton` — двойной код выделения

**Где:** `OcctView.cpp:661-668` vs `OcctView.cpp:739-746`

```
// SelectAtPixel
myContext->ClearSelected(false);
myContext->MoveTo(px, py, myView, false);
myContext->SelectDetected(AIS_SelectionScheme_Replace);
myContext->UpdateCurrentViewer();
myView->Redraw();

// OnMouseButton — то же самое
myContext->ClearSelected(false);
myContext->MoveTo(px, py, myView, false);
myContext->SelectDetected(AIS_SelectionScheme_Replace);
myContext->UpdateCurrentViewer();
myView->Redraw();
```

**Полная копипаста.** Фикс: `OnMouseButton` → вызов `SelectAtPixel(px, py)`.

**Оценка:** 5 минут.

---

---

### CR-318. `OnMouseButton` — двойной `myView->Redraw()`

**Где:** `OcctView.cpp:746, 754`

В ветке RELEASE с выделением — `Redraw()` в конце. И `myView->Redraw()` в ветке `myLeftDragging`. Ок, но проверять.

**WONTFIX.**

---

---

### CR-319. `Redraw()` — тонкая обёртка без проверки `IsNull` в самом деле

**Где:** `OcctView.cpp:801`

```
void OcctView::Redraw() {
    if (!myView.IsNull()) myView->Redraw();
}
```

Ок.

---

---

### CR-666. 🟠 `ViewCubeOverlay.cpp` — `computeUp` дублирует логику из `OcctView::SetCameraDirectionAndUp`

**Где:** `ViewCubeOverlay.cpp:385-394`

```
auto computeUp = [](const gp_Dir& dir) -> gp_Dir {
    gp_Dir zAxis(0, 0, 1);
    if (std::abs(dir.Dot(zAxis)) > 0.99) {
        return gp_Dir(0, 1, 0);
    }
    gp_Vec upProj = gp_Vec(zAxis) - gp_Vec(dir) * dir.Dot(zAxis);
    return gp_Dir(upProj);
};
```

**А в `OcctView::SetCameraDirectionAndUp` (CR-310):**
```
gp_Dir safeUp = up;
if (std::abs(dir.Dot(up)) > 0.99) {
    gp_Dir fallback(0, 0, 1);
    if (std::abs(dir.Dot(fallback)) > 0.9) fallback = gp_Dir(0, 1, 0);
    safeUp = fallback;
}
```

**Логика похожа, но разная.** `0.99` в обоих. `0.9` только в OcctView.

**Фикс:** единая функция `DirUtils::ComputeUpFor(dir)`.

**Оценка:** 30 минут.

---

---

### RAM-H4. OCCT AIS_InteractiveObject — не освобождаются
**Включает (merged):** RAM-H10, CR-237, CR-253, CR-740, CR-741

**Подозрение:** каждый `AddBox`/`DisplaySketchLine` создаёт AIS-объект в `OcctView`. Если документ закрыт, но AIS не очищен — OCCT держит.

**Проверить:**
```
grep -an "myContext->Remove\|AIS_InteractiveContext::Remove\|myContext->Erase" src/occt/OcctView.cpp
```

**Вероятность:** 🔴 **высокая.**

---

---

