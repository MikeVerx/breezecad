# BreezeCAD — Review: P2 / doc

**Записей:** 1

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-165. `OnSketchClick` — `myDocument->AddSketchLine` — вижу `myActiveSketchLabel`, но не вижу `BreezeDocument::GetType` проверки

Если `myActiveSketchLabel` — не эскиз, а что-то другое — `AddSketchLine` может отработать **некорректно**. Проверить сигнатуру и preconditions.

---

---

