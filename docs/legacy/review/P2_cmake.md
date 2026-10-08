# BreezeCAD — Review: P2 / cmake

**Записей:** 1

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-023. `-Wall -Wextra` не включены в CMake

**Где:** `CMakeLists.txt`
**Статус:** OPEN

Если бы были — CR-015 (`startPos`) дал бы warning. Значит — нет.

**Фикс:** добавить `-Wall -Wextra -Wpedantic` для GCC.
**Предупреждение:** может вскрыть **много** нового — делать отдельным коммитом.

**Оценка:** 30 минут диагностики.

---

---

