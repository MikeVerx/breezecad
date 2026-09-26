# P3 — МЕЛОЧИ — часть 1 из 1

**Записей в части:** 16

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-024. `if (scrollX < 0.0f) scrollX = 0.0f;` → `std::max`

**Где:** `Ribbon.cpp:39`

---


### CR-025. `RibbonContext::drawQat` — несимметрично с `drawTabs` (удалённым)

Логичнее `ctx.qatCtx`, а `Ribbon.cpp` вызовет `UI::Qat::Draw(...)`.

---


### CR-026. `ImGui::PushID(id)` при `id == nullptr` — UB

**Где:** `RibbonButton.cpp:52`
**Фикс:** `assert(id)` в debug.

---


### CR-027. `SketchTab.h` — неконсистентные дефолты

```
bool         inSketch = false;
LineStyle*   currentLineStyle = nullptr;   // без = nullptr — не единообразно
bool*        hasSketchStart   = nullptr;
```
На самом деле всё с `= nullptr`, но проверь глазами.

---


### CR-028. Комментарий про B-17 в `RibbonButton.cpp` врёт

**Где:** `RibbonButton.cpp:186`

```
// NOTE: static dummy — антипаттерн из RULES п.20 (записан как B-17 в REFACTOR_NOTES).
```

**Но B-17 в REFACTOR_NOTES нет.** Либо занести, либо убрать комментарий.

---


### CR-029. `[[nodiscard]]` на `Button` / `SingleButton`

B-07 в NOTES говорит «кнопки игнорируют return value» — `[[nodiscard]]` помог бы ловить на этапе компиляции.

---


### CR-030. `assert(ctx.state)` в debug-сборке

Связано с CR-008.

---


### CR-031. `ImVec2 startPos` — неиспользуемая в `Ribbon.cpp`?

Проверить есть ли в `Ribbon.cpp` неиспользуемые переменные.

---



### CR-078. `RibbonButton.cpp` — `ImVec2 mp = ImGui::GetIO().MousePos;` — можно `io.MousePos`


### CR-079. `RibbonButton.cpp` — `intptr_t` используется дважды без явного include

`intptr_t` из `<cstdint>`. Неявно приходит. **Явно добавить.**


### CR-080. `RibbonGroup.cpp` — `float cx = (pMin.x + pMax.x) * 0.5f - tsz.x * 0.5f;` — `0.5f` дважды, ок, но `std::midpoint`?

C++20 `std::midpoint` — не для float. Оставить.


### CR-081. `RibbonTabs.cpp` — `for (int i = 0; i < 4; i++)` — magic 4

`kRibbonTabCount = 4` или `std::size(tabs)`.


### CR-082. `SketchTab.cpp` — `struct LSItem { ... ImU32 bg; };` — `bg` в `IM_COL32` — считает цвет **в compile-time**, но лишний параметр в структуре. Можно вычислять из `style`.


### CR-083. `Application.cpp::Run()` — `try` в `while` без отступа

**Где:** `Application.cpp:1102`
```
while (...) {
  try {              // ← 2 пробела
    TRACE(...)       // ← 4 пробела
```
Стиль сломан. **Фикс:** clang-format.


### CR-084. `Application.cpp::Run()` — `std::snprintf` вместо безопасного `snprintf`

Уже ок. Но `#include <cstdio>` — есть?


### CR-085. `Application.cpp::Run()` — `myProjectNames[modIdx].c_str()` в `snprintf` — UTF-8 обрезка возможна

Если имя длинное (>100 симв.), обрезается посередине кириллического символа → кракозябры. **Фикс:** проверить размер.

---


