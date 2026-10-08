# BreezeCAD — Review: P3 / app

**Записей:** 3

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-083. `Application.cpp::Run()` — `try` в `while` без отступа

**Где:** `Application.cpp:1102`
```
while (...) {
  try {              // ← 2 пробела
    TRACE(...)       // ← 4 пробела
```
Стиль сломан. **Фикс:** clang-format.

---

### CR-084. `Application.cpp::Run()` — `std::snprintf` вместо безопасного `snprintf`

Уже ок. Но `#include <cstdio>` — есть?

---

### CR-085. `Application.cpp::Run()` — `myProjectNames[modIdx].c_str()` в `snprintf` — UTF-8 обрезка возможна

Если имя длинное (>100 симв.), обрезается посередине кириллического символа → кракозябры. **Фикс:** проверить размер.

---

---

