# BreezeCAD — Review: P1 / ribbon

**Записей:** 16

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-005. `Button` — 11 параметров, boolean blindness

**Где:** `RibbonButton.h`
**Статус:** OPEN

**Проблема:**
```
int Button(IconManager& icons,
           const char* id, const char* const* opts, int optCount, int& idx,
           const char* icon, bool enabled, bool big,
           const char* tooltip = nullptr, float iconScale = 1.0f);
```

Вызов:
```
Button(icons, "nav_pan", panOpts, 1, dummy, ICON_PAN, true, true, "Панорамирование");
```
Глядя на `true, true` — невозможно понять, что это `enabled, big`.

**Фикс:** `struct ButtonOpts` с named-полями. `Button(icons, opts)`.

**Оценка:** 1 час.

---

---

### CR-006. `int` возврат (0/1/2/3) — magic numbers

**Где:** `RibbonButton.h`
**Статус:** OPEN

**Проблема:**
```
int r = Button(...);
if (r == 1 || r == 3) { ... }
```
Что такое 1 и 3? Через месяц никто не вспомнит.

**Фикс:** `enum class BtnResult : uint8_t { None, Main, Arrow, Menu };` + `[[nodiscard]]`.

**Оценка:** 30 минут + правки call-sites.

---

---

### CR-007. `ctx.drawTabs` — мёртвое поле

**Где:** `RibbonContext.h:29`
**Статус:** OPEN

```
std::function<void()> drawTabs;   // никогда не вызывается
```

`Ribbon.cpp` зовёт `UI::Ribbon::DrawTabs(*ctx.icons, *ctx.state)` напрямую.

**Фикс:** удалить поле.

**Оценка:** 1 минута.

---

---

### CR-010. `splitMiddle` создаёт `std::vector<size_t>` каждый вызов

**Где:** `RibbonButton.cpp:~118`
**Статус:** OPEN

**Проблема:**
```
auto splitMiddle = [&](const std::string& s) {
    std::vector<size_t> spaces;   // heap alloc
    ...
};
```
Вызывается на каждой кнопке × каждый кадр × каждый `fs` в цикле.

**Фикс:** найти средний пробел без вектора — два прохода.
**Занести как OPT-05 в REFACTOR_NOTES.**

**Оценка:** 15 минут.

---

---

### CR-014. `if (!done) drawBottom({label}, baseSize * 0.45f);` — магия

**Где:** `RibbonButton.cpp`
**Статус:** OPEN

Почему 0.45? При базовом 16 → 7.2 px. При 32 → 14.4. Нечитаемо.

**Фикс:** `constexpr float kFallbackFontScale = 0.45f;` + комментарий почему.

**Оценка:** 5 минут.

---

---

### CR-015. `ImVec2 startPos = ImGui::GetCursorScreenPos();` — неиспользуемая

**Где:** `RibbonButton.cpp:54`
**Статус:** OPEN

Ни разу не используется. GCC с `-Wall` должен дать warning.

**Фикс:** удалить.

**Оценка:** 1 минута.

---

---

### CR-032. `RibbonButton.cpp` — `inArrow` проверяет только `X`, не `Y`

**Где:** `RibbonButton.cpp:65`

```
bool inArrow = hasArrow && (mp.x >= min.x + w);
```

Если курсор **ниже** кнопки, но по X в зоне стрелки → `inArrow = true`. Hover подсветит стрелку, хотя мышь вне кнопки.

**Фикс:** добавить `mp.y >= min.y && mp.y <= max.y`.

---

---

### CR-034. `RibbonButton.cpp` — магические множители шрифта

**Где:** `RibbonButton.cpp:148`

```
float sizes[] = { baseSize, baseSize * 0.82f, baseSize * 0.62f, baseSize * 0.52f };
```

**Фикс:** `constexpr float kFontScaleSteps[] = {1.0f, 0.82f, 0.62f, 0.52f};`.

---

---

### CR-035. `RibbonButton.cpp` — silent fail на `chevron-down`

**Где:** `RibbonButton.cpp:165`

```
GLuint arrowTex = icons.Get("chevron-down", 10);
if (arrowTex != 0) { ... }   // если нет — молча ничего
```

**Фикс:** debug-assert + warning в `IconManager` при NOT FOUND.

---

---

### CR-036. `RibbonButton.cpp` — `Tr(opts[i]).c_str()` — потенциальный dangling

**Где:** `RibbonButton.cpp:178`

Проверить `Localization.h`: `Tr` возвращает `const std::string&` или `std::string`? Если второе — `c_str()` от временного объекта = UB.

---

---

### CR-037. `RibbonGroup.cpp` — `TARGET_H = 76.0f`, а UI_LAYOUT говорит 78

**Где:** `RibbonGroup.cpp:13`
**Канон:** UI_LAYOUT.md §2.2 — Ribbon 78 px.
**Фикс:** согласовать. Возможно, 76 = высота без бордюра.

---

---

### CR-038. `RibbonGroup.cpp` — magic offsets

**Где:** `RibbonGroup.cpp:22-25`

```
dl->AddRectFilled(
    ImVec2(pMin.x - 6, pMin.y - 4),
    ImVec2(pMax.x + 6, pMax.y + 18),
    bg, 4.0f);
```

`-6, -4, +6, +18, 4.0f` — 5 magic.

---

---

### CR-040. `RibbonGroup.cpp` — `SameLine(0, 22)` magic

**Где:** `RibbonGroup.cpp:47`

---

---

### CR-041. `RibbonTabs.cpp` — `isLight` считается в цикле

**Где:** `RibbonTabs.cpp:22`
Вынести до цикла.

---

---

### CR-043. `RibbonTabs.cpp` — magic числа SvgIconBtn

**Где:** `RibbonTabs.cpp:41`
`SvgIconBtn(icons, "ribbon_plus", "plus", 16, 32, 26, ...)` — `16, 32, 26`.

---

---

### CR-044. `RibbonTabs.cpp` — `SetTooltip("Подключить модуль")` без `Tr()`

**Где:** `RibbonTabs.cpp:45`
Локализация обойдена.

---

---

