# BreezeCAD — Review: P2 / viewcube

**Записей:** 1

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-184. `DrawViewCube` — `Context` создаётся каждый кадр

Если `Context` содержит `IconManager&` или другие тяжёлые — heap-аллокации. Проверить определение `Context`.

---

