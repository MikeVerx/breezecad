# BreezeCAD — Review: P3 / misc

**Записей:** 3

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-029. `[[nodiscard]]` на `Button` / `SingleButton`

B-07 в NOTES говорит «кнопки игнорируют return value» — `[[nodiscard]]` помог бы ловить на этапе компиляции.

---

---

### CR-030. `assert(ctx.state)` в debug-сборке

Связано с CR-008.

---

---

### CR-082. `SketchTab.cpp` — `struct LSItem { ... ImU32 bg; };` — `bg` в `IM_COL32` — считает цвет **в compile-time**, но лишний параметр в структуре. Можно вычислять из `style`.

---

