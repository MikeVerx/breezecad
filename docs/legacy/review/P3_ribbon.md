# BreezeCAD — Review: P3 / ribbon

**Записей:** 9

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-024. `if (scrollX < 0.0f) scrollX = 0.0f;` → `std::max`

**Где:** `Ribbon.cpp:39`

---

---

### CR-025. `RibbonContext::drawQat` — несимметрично с `drawTabs` (удалённым)

Логичнее `ctx.qatCtx`, а `Ribbon.cpp` вызовет `UI::Qat::Draw(...)`.

---

---

### CR-026. `ImGui::PushID(id)` при `id == nullptr` — UB

**Где:** `RibbonButton.cpp:52`
**Фикс:** `assert(id)` в debug.

---

---

### CR-028. Комментарий про B-17 в `RibbonButton.cpp` врёт

**Где:** `RibbonButton.cpp:186`

```
// NOTE: static dummy — антипаттерн из RULES п.20 (записан как B-17 в REFACTOR_NOTES).
```

**Но B-17 в REFACTOR_NOTES нет.** Либо занести, либо убрать комментарий.

---

---

### CR-031. `ImVec2 startPos` — неиспользуемая в `Ribbon.cpp`?

Проверить есть ли в `Ribbon.cpp` неиспользуемые переменные.

---

---

### CR-078. `RibbonButton.cpp` — `ImVec2 mp = ImGui::GetIO().MousePos;` — можно `io.MousePos`

---

### CR-079. `RibbonButton.cpp` — `intptr_t` используется дважды без явного include

`intptr_t` из `<cstdint>`. Неявно приходит. **Явно добавить.**

---

### CR-080. `RibbonGroup.cpp` — `float cx = (pMin.x + pMax.x) * 0.5f - tsz.x * 0.5f;` — `0.5f` дважды, ок, но `std::midpoint`?

C++20 `std::midpoint` — не для float. Оставить.

---

### CR-081. `RibbonTabs.cpp` — `for (int i = 0; i < 4; i++)` — magic 4

`kRibbonTabCount = 4` или `std::size(tabs)`.

---

