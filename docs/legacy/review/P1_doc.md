# BreezeCAD — Review: P1 / doc

**Записей:** 2

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

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

---

### CR-161. `IsPointOnExistingVertex` / `SnapToKnownPoint` — дублируют логику обхода `TDF_ChildIterator`
**Включает (merged):** CR-138, CR-139, CR-140, CR-141

Обе функции пишут один и тот же паттерн:
```
TDF_ChildIterator it(sketchLabel, false);
for (; it.More(); it.Next()) {
    ... FindAttribute(TNaming_NamedShape::GetID(), ns) ...
    if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
        TopoDS_Edge e = TopoDS::Edge(s);
        TopExp_Explorer exp(e, TopAbs_VERTEX);
        ...
```

**Фикс:** `ForEachSketchVertex(sketchLabel, callback)`.

**Оценка:** 20 минут.

---

---

