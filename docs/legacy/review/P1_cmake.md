# BreezeCAD — Review: P1 / cmake

**Записей:** 4

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-213. `CMakeLists.txt` — нет явной переменной версии OCCT

**Где:** весь файл
Нужно знать, какую версию OCCT собираем против. Для диагностики кэша (`.bzc`).

**Фикс:** `message(STATUS "OCCT version: ${OpenCASCADE_VERSION}")`.

---

---

### CR-214. `CMakeLists.txt` — нет установки target для `install()`

**Где:** весь файл
Нет `install(TARGETS breezecad)`. Нельзя сделать `make install` / `ninja install`. Для portable-сборки — пока не критично, но для CI пригодится.

---

---

### CR-215. `CMakeLists.txt` — порядок source-файлов не группирован

**Где:** `CMakeLists.txt:55-108`
Файлы идут вперемешку. **Фикс:** группировка комментариями (`# app`, `# ui/ribbon`, `# doc`).

---

---

### CR-216. `CMakeLists.txt` — ImGui — `add_library(imgui STATIC ...)` без target_compile_options

ImGui под GCC нужен `-Wno-...` (иначе предупреждения). Сейчас их нет, но если включим `-Wall` — ImGui будет спамить.

**Фикс:** `target_compile_options(imgui PRIVATE -w)` (отключить warnings для чужой либы).

---

---

