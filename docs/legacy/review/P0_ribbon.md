# BreezeCAD — Review: P0 / ribbon

**Записей:** 14

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-002. `static int dummy = 0` — RULES п.20 + мина
**Включает (merged):** CR-045, CR-055, CR-056, CR-169, CR-393, CR-412, CR-531

**Где:** `RibbonButton.cpp::SingleButton`, `SketchTab.cpp::DrawSketchTab`
**Статус:** OPEN
**Категория:** RULES п.20

**Проблема:**
```
static int dummy = 0;
const char* opts[] = { key };
return Button(icons, id, opts, 1, dummy, ...);
```

**Почему мина:**
- `dummy` общий для всех вызовов `SingleButton`.
- Если `optCount > 1` когда-то — popup вернёт `3`, `dummy` изменится и **никогда не вернётся в 0**.

**В `SketchTab.cpp`** — та же проблема, `static int dummy` идёт в 20+ вызовов.

**Фикс:** убрать `static`. `int dummy = 0` на стеке — ноль цены.
**Заодно:** занести как **B-17** в `REFACTOR_NOTES` (в комментарии указано, но в файле нет).

**Оценка:** 10 минут.

---

---

### CR-003. Хардкод цветов в `RibbonButton.cpp` — RULES п.30
**Включает (merged):** CR-033, CR-039, CR-042, CR-046, CR-048, CR-124, CR-168, CR-279, CR-292, CR-344, CR-356, CR-365, CR-375, CR-406, CR-413, CR-445, CR-567, CR-590

**Где:** `RibbonButton.cpp:34-42`
**Статус:** OPEN
**Категория:** RULES п.30

**Проблема:**
```
if (isLight) {
    bgHover  = ImVec4(0.760f, 0.800f, 0.860f, 0.6f);
    bgActive = ImVec4(0.640f, 0.720f, 0.840f, 0.8f);
} else {
    bgHover  = ImVec4(0.35f, 0.55f, 0.85f, 0.35f);
    bgActive = ImVec4(0.35f, 0.55f, 0.85f, 0.55f);
}
```

RULES п.30: «Всё окрашивается от темы. Никаких хардкод-цветов в UI-коде.»

**Фикс:**
1. В `ThemeTokens` — `ribbonBtnHover`, `ribbonBtnActive`, `ribbonBtnHoverDisabled`, `ribbonBtnActiveDisabled`, `arrowBg`, `arrowBgDisabled`.
2. В `RibbonButton.cpp` — тянуть из темы.
3. Проверить 3 темы (Светлая / Тёмная / Авто).

**Оценка:** 40–50 минут + проверка.

---

---

### CR-004. `LineStyle*` в `RibbonContext` — утечка слоя doc в UI
**Включает (merged):** CR-486, CR-549, CR-559, CR-596, CR-646, CR-655

**Где:** `RibbonContext.h`, `SketchTab.h`
**Статус:** OPEN
**Категория:** архитектура (`ARCHITECTURE.md §2`)

**Проблема:**
```
#include "doc/BreezeDocument.h"   // LineStyle
...
LineStyle* currentLineStyle = nullptr;
```

UI-модуль жёстко зависит от `doc/BreezeDocument.h`. Если `LineStyle` переедет в `kernel/` — сломается ribbon.

**Фикс (варианты):**
- **A.** `int* currentLineStyleIdx` — UI знает 0/1/2.
- **B.** Колбэки `setLineStyle(int)` + `getLineStyle()` — UI не знает семантику.
- **C.** Enum в `util/`.

Рекомендую **B** — чище по слоям.

**Оценка:** 20 минут.

---

---

### CR-008. nullptr-проверки неконсистентны
**Включает (merged):** CR-111, CR-171, CR-307, CR-308, CR-382, CR-441, CR-442, CR-443, CR-595, CR-715

**Где:** `Ribbon.cpp:34, 46, 50`
**Статус:** OPEN

**Проблема:**
```
if (ctx.icons && ctx.state) UI::Ribbon::DrawTabs(...);   // проверка есть
...
switch (ctx.state->activeTab) {                           // проверки нет
    case 0: UI::Ribbon::DrawModelTab(*ctx.icons, ...);   // и тут нет
```

**Фикс:** определиться с контрактом:
- **A.** «`ctx.state` всегда не-null» — `assert` в debug, проверки убрать.
- **B.** «может быть null» — проверять везде.

Рекомендую **A**.

**Оценка:** 10 минут.

---

---

### CR-020. `206` — магическое число в `Ribbon.cpp`
**Включает (merged):** CR-058, CR-059, CR-099, CR-170, CR-195, CR-305, CR-366, CR-368, CR-378, CR-397, CR-410, CR-419, CR-420, CR-453, CR-456, CR-468, CR-476, CR-484, CR-496, CR-504, CR-510, CR-527, CR-539, CR-564, CR-565, CR-566, CR-571, CR-574, CR-575, CR-576, CR-583, CR-584, CR-588, CR-589, CR-606, CR-622, CR-641, CR-650, CR-664, CR-670, CR-671

**Где:** `Ribbon.cpp:15`
**Статус:** OPEN

```
ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 206), ImGuiCond_Always);
```

В `UI_LAYOUT.md §2.2`: QAT 32 + Tabs 32 + Ribbon 78 = **142**. А тут **206**.

**Расхождение с каноном — 64 px.** Либо `UI_LAYOUT` устарел, либо 206 — ошибка.

**Фикс:**
1. Разобраться: 206 или 142?
2. `constexpr float kRibbonTotalHeight = ...;`
3. Актуализировать `UI_LAYOUT.md`.

**Оценка:** 30 минут с проверкой на скринах.

---

---

### CR-051. `ViewTab.cpp` — `int zoomIdx = 0;` сбрасывается каждый кадр

**Где:** `ViewTab.cpp:28, 36`

**Проблема:**
```
int zoomIdx = 0;   // локальная
Button(icons, "nav_zoom", zoomOpts, 4, zoomIdx, ...);
```

Пользователь выбирает «Показать всё» → следующий кадр → `zoomIdx = 0` → снова «Зумировать». **Выбор не сохраняется.**

**Фикс:** перенести в `RibbonState` как `zoomToolIndex`, `orbitToolIndex`.

---

---

### CR-052. `ViewTab.cpp` — несогласованность `state` vs локальных `idx`

**Где:** `ViewTab.cpp`

| Кнопка | Источник индекса | Сохраняется? |
|---|---|---|
| `view_style` | `state.displayStyleIndex` | ✅ |
| `view_bg` | `state.backgroundIndex` | ✅ |
| `nav_zoom` | `int zoomIdx = 0` | ❌ |
| `nav_orbit` | `int orbIdx = 0` | ❌ |
| `nav_pan` | `dummy` | ❌ |

**Фикс:** все индексы — в `RibbonState`.

---

---

### CR-172. `EnterSketch`/`ExitSketch` — magic `activeTab = 0/1`

**Где:** `Application.cpp:843, 860`

```
myRibbonState.activeTab = 1;   // Sketch tab
...
myRibbonState.activeTab = 0;   // 3D Model tab
```

`0` и `1` — magic. Связано с **CR-019** (enum class Tab).

**Фикс:** `myRibbonState.activeTab = static_cast<int>(Tab::Sketch);`.

**Оценка:** входит в CR-019.

---

---

### CR-321. `ThemeTokens` — нет дефолтных значений → UB

**Где:** `src/ui/theme/ThemeTokens.h:12-44`

**Проблема:** все 28 полей — `ImU32` **без инициализации**.
```
struct ThemeTokens {
    ImU32 bgTitle;      // ← мусор
    ImU32 bgRibbon;     // ← мусор
    ...
};
```

Если создать `ThemeTokens t;` — все поля содержат мусор. Применение до `ApplyTheme` → случайные цвета или краш.

**Фикс:** `= 0` у каждого поля **или** конструктор с дефолтами.

**Оценка:** 5 минут.

---

---

### CR-322. `ThemeTokens` — нет токенов для ribbon-кнопок

**Где:** `ThemeTokens.h`

**Проблема:** `RibbonButton.cpp` использует **6 захардкоженных цветов** (CR-003):
- `bgHover`, `bgActive` — светлая/тёмная
- `arrowBg`, `arrowBgDisabled` — светлая/тёмная
- `IM_COL32(140, 140, 140, 160)` — fallback-иконка

В `ThemeTokens` — **ни одного** из них.

**Фикс:** добавить:
```
// Ribbon кнопки
ImU32 ribbonBtnHover;
ImU32 ribbonBtnActive;
ImU32 ribbonBtnHoverDisabled;
ImU32 ribbonBtnActiveDisabled;
ImU32 ribbonArrowBg;
ImU32 ribbonArrowBgDisabled;
ImU32 ribbonIconFallback;
```

**Оценка:** 10 минут (только структура) + 1 час (заполнение в 3 темах + правки RibbonButton).

---

---

### CR-328. `ThemeTokens` — поля не задокументированы

**Где:** `ThemeTokens.h:13-42`

`bgTitle`, `bgCardBg`, `homeBtnLabel`, `homeBtnBg` — **что это**? При чтении `RibbonButton.cpp`, где `bgHover = tokens.bgCardBg` — непонятно.

**Фикс:** однострочный комментарий к каждому полю.

**Оценка:** 15 минут.

---

---

### CR-363. `IconTint` — нет функции для ribbon-arrow tint

**Где:** `IconTint.cpp`

В `RibbonButton.cpp:170` — стрелка ▼ рисуется тем же `tint`. Но в disabled-состоянии стрелка **не должна менять прозрачность** так же, как иконка — она часть фона.

Пока работает, но хрупко. **Отложено до Фазы 2.**

**WONTFIX.**

---

---

### CR-730. 🔴 `Tr()` возвращает `std::string` BY VALUE — heap-аллокация на каждый вызов

**Где:** `Localization.h:18`

```
std::string Tr(const std::string& key) const;
```

**Связано с CR-703** (Utf8 возвращает по значению). Проблема **каскадная**:

**Каждый вызов `Tr()` в hot-path создаёт копию:**
- `DrawDocumentTree` для 500 узлов × каждый кадр = 30 000 аллокаций/сек.
- `RibbonTab` для 50 кнопок × 60 FPS = 3 000/сек.
- `QAT` для 12 кнопок × 60 FPS = 720/сек.

**Фикс:**
```
const std::string& Tr(const std::string& key) const;
```

**НО:** нужно возвращать **временный объект**, если ключа нет → ссылка не подходит. **Правильно:**
```
std::string_view Tr(std::string_view key) const;  // C++17
```
Или — `const char* Tr(...)` с указателем на interned-строку.

**Или:** `void Tr(const std::string& key, std::string& out)` — reuse буфера.

**Оценка:** 3 часа (переписать + обновить ~150 call-sites).

---

---

### RAM-H5. `std::function` closure в `DrawRibbon` — накапливаются
**Включает (merged):** CR-001

**Связь с CR-001.** Каждый кадр — новые closures. Уничтожаются в конце кадра, но heap-фрагментация растёт.

**Вероятность:** 🟡 низкая (фрагментация, не рост).

---

---

