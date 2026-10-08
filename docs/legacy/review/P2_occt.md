# BreezeCAD — Review: P2 / occt

**Записей:** 1

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-130. `OnExportSTEP` — `Transfer` по одному shape

**Где:** `Application.cpp:399-403`
```
for (const auto& s : shapes) {
    if (writer.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) { ... }
}
```

Можно собрать `TopoDS_Compound` и передать одним вызовом — быстрее.

**Оценка:** 15 минут.

---

---

