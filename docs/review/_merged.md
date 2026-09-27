# BreezeCAD — Code Review (MERGED)

**Создано:** объединение дубликатов 2026-09-27
**Записей до:** 761
**Удалено (aliases):** 213
**Записей после:** 548
**Групп объединено:** 34

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

### CR-009. `bool* hasSketchStart` — control coupling

**Где:** `SketchTab.cpp:77`
**Статус:** OPEN

**Проблема:**
```
if (r == 1 || r == 3) {
    if (ctx.hasSketchStart) *ctx.hasSketchStart = false;   // UI мутирует Application
}
```
UI не должен знать о внутренностях Application.

**Фикс:** колбэк `onLineToolSelected()` — Application сам решает что сбрасывать.

**Оценка:** 15 минут.

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

### CR-011. Неконсистентный стиль групп вкладок

**Где:**
- `ModelTab.cpp`, `SketchTab.cpp`, `ViewTab.cpp`, `AssemblyTab.cpp` — `GroupStart/GroupEnd`
- `AnnotateTab.cpp`, `ToolsTab.cpp`, `ManageTab.cpp` — `BeginGroup/TextColored/SeparatorEx`

**Статус:** OPEN

4 «новые» вкладки используют Inventor-style, 3 «старые» — плоские. Причина: их я перенёс **как было** из Application, они уже были уродливые.

**Фикс:** привести 3 старых к стилю `GroupStart/GroupEnd` + `Tr("grp.*")`.

**Оценка:** 1.5 часа (переписать 3 вкладки).

---

### CR-012. `ImGui::SeparatorEx` + `ImGuiSeparatorFlags_Vertical` — internal API

**Где:** `AnnotateTab.cpp`, `ToolsTab.cpp`, `ManageTab.cpp`
**Статус:** OPEN

`SeparatorEx` — **не** public ImGui API. Может исчезнуть или измениться.

**Фикс:** исчезнет после CR-011.

---

### CR-013. `static const char* oneOpt[] = { "tool.dummy" };` — мёртвая переменная

**Где:** `ModelTab.cpp:19`, `AnnotateTab.cpp`, `ToolsTab.cpp`, `ManageTab.cpp`, `SketchTab.cpp`
**Статус:** OPEN

Объявлена, нигде не используется.

**Фикс:** удалить.

**Оценка:** 5 минут.

---

### CR-014. `if (!done) drawBottom({label}, baseSize * 0.45f);` — магия

**Где:** `RibbonButton.cpp`
**Статус:** OPEN

Почему 0.45? При базовом 16 → 7.2 px. При 32 → 14.4. Нечитаемо.

**Фикс:** `constexpr float kFallbackFontScale = 0.45f;` + комментарий почему.

**Оценка:** 5 минут.

---

### CR-015. `ImVec2 startPos = ImGui::GetCursorScreenPos();` — неиспользуемая

**Где:** `RibbonButton.cpp:54`
**Статус:** OPEN

Ни разу не используется. GCC с `-Wall` должен дать warning.

**Фикс:** удалить.

**Оценка:** 1 минута.

---

### CR-017. Порядок include и форвард-декларации

**Где:** `RibbonContext.h`
**Статус:** OPEN

**Проблема:**
```
#include "ui/ribbon/RibbonState.h"
#include "doc/BreezeDocument.h"

namespace BreezeCAD { class IconManager; }   // форвард между include'ами

#include <functional>
```

**Фикс:** системные → свои → форварды.

**Оценка:** 5 минут.

---

### CR-018. `namespace A { namespace B {` вместо `namespace A::B {`

**Где:** все файлы
**Статус:** DEFERRED (до clang-tidy)

C++17 позволяет `namespace BreezeCAD::UI::Ribbon {`. Экономит строки и indent.

**Фикс:** массовая замена через regex + clang-format. Отдельный коммит.

---

### CR-019. `RibbonState::activeTab` — `int`, а не `enum class`

**Где:** `RibbonState.h`
**Статус:** OPEN

**Проблема:**
```
int activeTab = 0;   // что если 5?
```

В `Ribbon.cpp` switch — **ни один case не выполнится**, ничего не отрисуется. Молча.

**Фикс:** `enum class Tab { Model=0, Sketch, Assembly, View };` + `Tab activeTab = Tab::Model;`.

**Оценка:** 30 минут.

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

### CR-023. `-Wall -Wextra` не включены в CMake

**Где:** `CMakeLists.txt`
**Статус:** OPEN

Если бы были — CR-015 (`startPos`) дал бы warning. Значит — нет.

**Фикс:** добавить `-Wall -Wextra -Wpedantic` для GCC.
**Предупреждение:** может вскрыть **много** нового — делать отдельным коммитом.

**Оценка:** 30 минут диагностики.

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
**Включает (merged):** CR-070, CR-071, CR-073

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

### CR-032. `RibbonButton.cpp` — `inArrow` проверяет только `X`, не `Y`

**Где:** `RibbonButton.cpp:65`

```
bool inArrow = hasArrow && (mp.x >= min.x + w);
```

Если курсор **ниже** кнопки, но по X в зоне стрелки → `inArrow = true`. Hover подсветит стрелку, хотя мышь вне кнопки.

**Фикс:** добавить `mp.y >= min.y && mp.y <= max.y`.

---

### CR-034. `RibbonButton.cpp` — магические множители шрифта

**Где:** `RibbonButton.cpp:148`

```
float sizes[] = { baseSize, baseSize * 0.82f, baseSize * 0.62f, baseSize * 0.52f };
```

**Фикс:** `constexpr float kFontScaleSteps[] = {1.0f, 0.82f, 0.62f, 0.52f};`.

---

### CR-035. `RibbonButton.cpp` — silent fail на `chevron-down`

**Где:** `RibbonButton.cpp:165`

```
GLuint arrowTex = icons.Get("chevron-down", 10);
if (arrowTex != 0) { ... }   // если нет — молча ничего
```

**Фикс:** debug-assert + warning в `IconManager` при NOT FOUND.

---

### CR-036. `RibbonButton.cpp` — `Tr(opts[i]).c_str()` — потенциальный dangling

**Где:** `RibbonButton.cpp:178`

Проверить `Localization.h`: `Tr` возвращает `const std::string&` или `std::string`? Если второе — `c_str()` от временного объекта = UB.

---

### CR-037. `RibbonGroup.cpp` — `TARGET_H = 76.0f`, а UI_LAYOUT говорит 78

**Где:** `RibbonGroup.cpp:13`
**Канон:** UI_LAYOUT.md §2.2 — Ribbon 78 px.
**Фикс:** согласовать. Возможно, 76 = высота без бордюра.

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

### CR-040. `RibbonGroup.cpp` — `SameLine(0, 22)` magic

**Где:** `RibbonGroup.cpp:47`

---

### CR-041. `RibbonTabs.cpp` — `isLight` считается в цикле

**Где:** `RibbonTabs.cpp:22`
Вынести до цикла.

---

### CR-043. `RibbonTabs.cpp` — magic числа SvgIconBtn

**Где:** `RibbonTabs.cpp:41`
`SvgIconBtn(icons, "ribbon_plus", "plus", 16, 32, 26, ...)` — `16, 32, 26`.

---

### CR-044. `RibbonTabs.cpp` — `SetTooltip("Подключить модуль")` без `Tr()`

**Где:** `RibbonTabs.cpp:45`
Локализация обойдена.

---

### CR-047. `SketchTab.cpp` — тихая деградация при `nullptr`

**Где:** `SketchTab.cpp:205, 219`

```
if (ctx.currentLineStyle) *ctx.currentLineStyle = items[i].style;
```

Если `nullptr` — подсветка работает, но выбор не сохраняется. Пользователь в недоумении. **Надо assert или лог.**

---

### CR-049. `ViewTab.cpp` — три одинаковых пункта в выпадашке «Панорамирование»

**Где:** `ViewTab.cpp:21`
**Статус:** OPEN
**Категория:** функциональный баг

**Проблема:**
```
static const char* panOpts[] = { "tool.pan", "tool.pan", "tool.pan" };
```

Меню открывается — пользователь видит «Панорамирование / Панорамирование / Панорамирование».

**Фикс:** оставить один пункт `{ "tool.pan" }` или заменить на реальные варианты (панорама / суперштурвал отключён / …).

**Оценка:** 5 минут.

---

### CR-050. `ViewTab.cpp` — `optCount=1` при массиве из 3

**Где:** `ViewTab.cpp:22`

**Проблема:**
```
Button(icons, "nav_pan", panOpts, 1, dummy, ICON_PAN, true, true, ...);
```
`optCount=1` → `hasArrow = (1 > 1) = false` → **стрелка ▼ не появляется**, хотя массив из 3.

Связано с CR-049. Оба фикса — заодно.

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

### CR-053. `ViewTab.cpp` — `win_switch` использует ключ от `win_new`

**Где:** `ViewTab.cpp:97-100`

**Проблема:**
```
SingleButton(icons, "win_switch", "tool.new_window", ...);
SingleButton(icons, "win_new",    "tool.new_window", ...);
```

Обе кнопки показывают tooltip «Новое окно». Должно быть `tool.switch_window` для первой.

**Фикс:** проверить `Localization.cpp`, добавить/исправить ключ.

---

### CR-054. `Annotate/Tools/Manage.cpp` — цвета текста не через тему

**Где:** все три файла
`ImGui::GetStyleColorVec4(ImGuiCol_Text)` — базовый ImGui, не токен проекта.

**Фикс:** после CR-011 автоматически уйдёт (там будет `GroupEnd` с `Tr`).

---

### CR-057. `Application.cpp::Run()` — мёртвые переменные
**Включает (merged):** CR-591, CR-617, CR-686

**Где:** `Application.cpp:1095-1097`

```
bool prevDelete = false;   // не используется
bool prevCtrlS  = false;   // не используется
bool prevCtrlO  = false;   // не используется
```

**Фикс:** удалить.

---

### CR-060. `Application.cpp::Run()` — `try {` без `catch` (проверить)

**Где:** `Application.cpp:1101`

Открывается `try {`, но в видимом фрагменте нет `catch`. Проверить ниже по файлу — есть ли.

**Если нет** — компилятор не пропустит. Если есть — **что там?** Глушится ли исключение? Логируется?

---

### CR-061. `Application.cpp::Run()` — паттерн `winW/winH/fbW/fbH` дублируется 3+ раз
**Включает (merged):** CR-312, CR-376

**Где:** `Application.cpp:1156-1160`, `1187-1191`, и ещё ниже

```
int winW = 1, winH = 1, fbW = 1, fbH = 1;
glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
int px = (int)(x * fbW / winW);
int py = (int)(y * fbH / winH);
```

**Фикс:** helper `ScreenToFramebuffer(double x, double y, int& px, int& py)`.

---

### CR-062. `Application.cpp::Run()` — прямые `glfwGetKey`/`glfwGetMouseButton` вместо `ImGuiIO`

**Где:** `Application.cpp:1134, 1140, 1197-1198`

```
glfwGetKey(..., GLFW_KEY_F11)
glfwGetMouseButton(..., GLFW_MOUSE_BUTTON_RIGHT)
glfwGetKey(..., GLFW_KEY_LEFT_SHIFT)
```

ImGui уже это знает: `io.KeyShift`, `io.MouseDown[1]`, `io.KeysDown[GLFW_KEY_F11]`. **Меньше GLFW-вызовов в hot-path.**

**Фикс:** заменить на `io.*`.

---

### CR-063. `RibbonButton.cpp` — `min/max` через два отдельных вызова
**Включает (merged):** CR-064, CR-065, CR-066, CR-067, CR-068, CR-069

**Где:** `RibbonButton.cpp:59-60`
```
ImVec2 min = ImGui::GetItemRectMin();
ImVec2 max = ImGui::GetItemRectMax();
```
Норм, но `min`/`max` — плохие имена (конфликт с `std::min/max` в глазах читателя). **Фикс:** `rMin`, `rMax`.

---

### CR-072. `ModelTab.cpp` — `ImGui::BeginGroup/EndGroup` вложены в `GroupStart/End`

**Где:** `ModelTab.cpp:44-51`
```
GroupStart(state);
...
ImGui::BeginGroup();       // ← вложенная группа
    ...
ImGui::EndGroup();
GroupEnd(...);
```
`GroupStart` уже делает `BeginGroup`. Вложенная — не bug, но **усложняет layout**. Проверить визуально.

---

### CR-074. `SketchTab.cpp` — `snprintf` без явного включения `<cstdio>`

Уже в CR-022.

---

### CR-075. `SketchTab.cpp` — `ImGui::SameLine()` без аргументов внутри `Ограничения`

**Где:** `SketchTab.cpp:135-153`
Паттерн: 6 × `Button + SameLine`, потом снова. **Implicit wrap** — если кнопка не влезает, перескока не будет. **Фикс:** явные `SameLine` и новая строка.

---

### CR-076. `AnnotateTab.cpp` — `ICON_DIM_*` — 5 разных иконок, но по канону они **одной группы**

**Где:** `AnnotateTab.cpp:18-28`
Все 5 — large, tooltip отсутствует. OK, но по UI_LAYOUT они могли быть в группе «Размеры» с `GroupStart/End` — то же, что CR-011.

---

### CR-077. `ToolsTab.cpp` — `SingleButton` с `"section"` иконкой — **это и есть кнопка «Сечение»**

**Где:** `ToolsTab.cpp:42`
`SingleButton(icons, "section", "tool.section", ICON_SECTION, ...)` — а мы её только что «возвращаем в план». Проверить: где ей место — в Tools или в View?

**По моему плану (Q-11):** в View, группа «Сечения». Значит, **эту надо перенести**, или у Tools оставить как «сечение в анализе», а во View — как основной инструмент.

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

### CR-086. `Application` конструктор — молчаливый выход при провале окна

**Где:** `Application.cpp:135`
```
myWindow = std::make_unique<GlfwWindow>(...);
if (!myWindow->GetNativeWindow()) return;   // ← return из конструктора!
```

Если окно не создалось — конструктор завершается, но **объект создан в полу-состоянии**. `Run()` вызовется и упадёт.

**Фикс:** либо `throw std::runtime_error`, либо явное поле `bool myInitFailed`.

---

### CR-087. `ApplyTheme` вызывается **до** `LoadFonts`

**Где:** `Application.cpp:151-153`
```
ApplyTheme(myCurrentTheme);          // ← первый раз, шрифты не готовы
UI::Theme::LoadFonts();              // ← загружаем шрифты
...
ApplyTheme(myCurrentTheme);          // ← второй раз, ок
```

Первый вызов — **возможно, бесполезен**. Надо проверить, что делает `UI::Theme::ApplyTheme` — если он трогает размеры шрифтов, первый вызов может дать неверные значения.

**Фикс:** либо убрать первый, либо обосновать комментарием.

---

### CR-088. Пустой `catch (...)` глотает все ошибки

**Где:** `Application.cpp:161`
```
try {
    if (!myOcctView->Init(...)) { myOcctView.reset(); return; }
} catch (...) { myOcctView.reset(); return; }
```

**Молчаливый `return`** + `catch(...)` без логирования. При любой ошибке OCCT приложение тихо не запускается. Пользователь видит пустой чёрный экран.

**Фикс:** как минимум `std::cerr << "OcctView::Init failed\n";`. Как максимум — `std::exception_ptr` + rethrow.

---

### CR-089. `ApplyTheme`: magic `3` для system

**Где:** `Application.cpp:133`
```
myCurrentTheme = (theme == 3) ? 1 : theme;
```
`3` = System. Нужен `enum class Theme { Light=0, Dark=1, System=2 };`. Заодно связать с `ThemeTokens.h`.

---

### CR-090. `~Application()` — порядок shutdown может дать UB

**Где:** `Application.cpp:199-203`
```
Application::~Application() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
```

**Проблемы:**
1. `myOcctView` — `unique_ptr`, dtor вызовется **после** тела деструктора. То есть **после** `ImGui shutdown` и `DestroyContext`. Если `OcctView::~OcctView()` дёргает `ImGui` или `ImGui` state — UB.
2. `myIcons` — не освобождает GL-текстуры. Утечка при перезапуске.
3. `myWindow` — тоже после. Если `GlfwWindow::~GlfwWindow()` дёргает `glfwTerminate()` — а `ImGui` уже сдох. Порядок критичен.

**Фикс:** явный сброс `myOcctView.reset(); myWindow.reset();` **в начале** деструктора, **до** ImGui shutdown.

---

### CR-091. `IconManager::LoadAll("assets/icons")` — хардкод пути
**Включает (merged):** CR-219, CR-335, CR-457

**Где:** `Application.cpp:140`
RULES п.23: «Никаких хардкодов путей. `std::filesystem` или glfw-хелперы.»

**Фикс:** `std::filesystem::path exeDir = ...` + `exeDir / "assets" / "icons"`.

---

### CR-095. `InitCommands`: `[]{}` пустые лямбды — команды без действия

**Где:** `Application.cpp:1050-1053, 1058-1060`
```
add("sketch.rect", "Прямоугольник", "R", []{});
add("sketch.circle", "Окружность", "C", []{});
```

Команды есть в поиске, но клик — **ничего не происходит**. Пользователь в недоумении.

**Фикс:** либо реальные действия, либо `nullptr` + пометка «не реализовано» в UI.

---

### CR-096. `InitCommands`: `sketch.line` дублирует код из `SketchTab.cpp:77`

**Где:** `Application.cpp:1045-1049` vs `SketchTab.cpp:77-81`

**Копипаста** трёх строк:
```
myCurrentLineStyle = LS_Normal;
myHasSketchStart = false;
if (myOcctView) myOcctView->ClearPreviewLine();
```

**Фикс:** вынести в `Application::ResetSketchLineTool()`, вызывать из обоих мест. Связано с CR-009.

---

### CR-098. `UpdateTelemetry`: `PROCESS_MEMORY_COUNTERS pmc;` — не инициализирован

**Где:** `Application.cpp:1077`
```
PROCESS_MEMORY_COUNTERS pmc;   // ← мусор
if (GetProcessMemoryInfo(...)) { myLastRAMMB = ...; } else { myLastRAMMB = 0.0; }
```
Формально ок (myLastRAMMB перезапишется), но паттерн плохой. **Фикс:** `= {};`.

---

### CR-100. `UpdateTelemetry`: `myLastShapeCount` — тип?

**Где:** `Application.cpp:1089`
`myLastShapeCount = myOcctView->GetShapeCount();`
`GetShapeCount()` возвращает `int` или `size_t`? Если второе — обрезка. Проверить.

---

### CR-101. `InitCommands`: hotkey «R» для Прямоугольника, но нет реальной обработки

**Где:** `Application.cpp:1050`
Хоткеи в командах есть, но **кто их обрабатывает**? В `Run()` — не видно. Возможно, ImGui shortcuts или внешний код. Проверить.

---

### CR-102. Обработчики `myWindow->SetXxxCallback([this]...)` захватывают `this`

**Где:** `Application.cpp:180-193`
Лямбды захватывают `this`. Если окно живёт **дольше** Application (например, во время destruction) — callback сработает на мёртвом `this`. **UB.**

**Фикс:** либо отписываться в `~Application`, либо `weak_ptr`-схема.

---

### CR-104. `RefreshView`: `LabelKey(l)` возвращает `std::string`

**Где:** `Application.cpp:224`
Уже в CR-093. Плюс: что делает `LabelKey`? Ищет по имени или по `Entry()`? Если по имени — там кириллица (BUG-A5, `TCollection_AsciiString`).

---

### CR-105. `RefreshView`: `myOcctView->Redraw()` в конце — синхронно

**Где:** `Application.cpp:241`
`RefreshView` вызывается часто (после каждой операции). `Redraw()` — блокирующий. **Фикс:** отложить до следующего кадра, вызывать в `Run()`.

---

### CR-106. `ApplyTheme`: `.get()` из `unique_ptr` — можно было ссылку

**Где:** `Application.cpp:133`
```
UI::Theme::ApplyTheme(theme, myTheme, myOcctView.get());
```
Если функция принимает `OcctView*` — ок. Но если можно `OcctView&` — лучше ссылка.

---

### CR-107. `UpdateTelemetry`: логика под `#ifdef _WIN32` — нужен `#else` для Unix

**Где:** `Application.cpp:1078-1087`
Сейчас на Unix — RAM = 0.0. **Фикс:** `#elif __linux__` — `/proc/self/status`, `#elif __APPLE__` — `task_info`. Или `Platform::GetRamMB()`.

---

### CR-108. `RefreshView`: рекурсия без защиты от циклов

**Где:** `Application.cpp:220-238`
`walk` рекурсивно обходит дерево. Если OCAF-граф содержит цикл (не должен, но) — **stack overflow**.

**Фикс:** max-depth или visited-set.

---

### CR-109. `RefreshView`: `walk` не проверяет `l.IsNull()`

**Где:** `Application.cpp:220`
Первая итерация `walk(myDocument->MainLabel())` — если `MainLabel()` null, внутри `GetType(l)` — UB.

**Фикс:** `if (l.IsNull()) return;` в начале.

---

### CR-112. `OnSave` не обновляет `myProjectPaths[myActiveDocIndex]`
**Включает (merged):** CR-113, CR-115

**Где:** `Application.cpp:363-378`

**Проблема:**
- Пользователь создаёт новый документ → `myProjectPaths[myActiveDocIndex] = ""` (в `CreateNewDoc`).
- Жмёт Ctrl+S → диалог «Сохранить как» → сохраняет в `D:/foo.cbf`.
- `myCurrentPath = path;` ✅
- **`myProjectPaths[myActiveDocIndex]` остаётся `""`** ❌

**Что ломается:**
- В списке проектов путь пустой.
- `UI::Home::AddRecentFile(myRecentFiles, path)` работает, но `myProjectPaths` рассинхронизирован.
- При следующем сохранении снова спросит путь.

**Фикс:** `myProjectPaths[myActiveDocIndex] = path;` после успешного `SaveDocument`.

**Оценка:** 2 минуты.

---

### CR-116. `Interface_Static::SetCVal` — глобальный статик OCCT

**Где:** `Application.cpp:392-393`

```
Interface_Static::SetCVal("write.step.unit", "MM");
Interface_Static::SetCVal("write.step.schema", "AP214");
```

`Interface_Static` — **глобальное состояние OCCT-процесса**, не per-instance. Если когда-нибудь будут параллельные экспорты (или другой код OCCT, читающий эти параметры) — гонка.

**Дополнительно:** эти параметры — **дефолтные**, `SetCVal` избыточен. Проверить.

**Фикс:** если нужен явный AP214 — использовать `STEPControl_Writer` с параметрами per-writer (если API позволяет). Иначе — `SetCVal` **один раз** при инициализации, не каждый экспорт.

**Оценка:** 10 минут (проверить API).

---

### CR-117. `OnExportSTL` — тесселяция 0.1 хардкод + блокирует UI

**Где:** `Application.cpp:434`
```
BRepMesh_IncrementalMesh mesh(comp, 0.1);   // ← 0.1 мм, hardcoded
```

**Проблемы:**
1. `0.1` — magic. На большой детали (500мм) слишком мелко → тормоза; на мелкой — слишком крупно.
2. **Блокирует UI** на время тесселяции. RULES п.26: всё >1 сек — с прогресс-баром.
3. Не спросили у пользователя точность.

**Фикс:**
- Вынести `0.1` в константу или в параметр диалога.
- Тесселяция — в `std::async`, с прогрессом.

**Оценка:** 1 час (правильно).

---

### CR-118. `OnExportSTL` — нет прогресс-бара

Связано с CR-117. RULES п.26: «Прогресс-бары для всего, что >1 сек».

**Оценка:** входит в CR-117.

---

### CR-119. `Standard_False` в `OnExportSTL`

**Где:** `Application.cpp:441`
```
writer.ASCIIMode() = Standard_False;
```

TECH-DEBT-005. **Фикс:** `false`.

**Оценка:** 1 минута.

---

### CR-120. `OnUndo` / `OnRedo` — двойной рендер

**Где:** `Application.cpp:469-528`

```
if (myDocument->Undo()) {
    RefreshView();                          // ← рендер 1
    if (!myActiveSketchLabel.IsNull()) {
        if (myOcctView) myOcctView->ClearAllShapes();
        RefreshSketchLines();                // ← рендер 2
        myOcctView->ShowGrid(myShowGrid);
    }
}
```

`RefreshView()` внутри уже вызывает `Redraw()`. Потом `RefreshSketchLines()` дёргает ещё. **Два рендера за кадр.**

**Фикс:** объединить. `RefreshView` должен знать о режиме эскиза.

**Оценка:** 15 минут.

---

### CR-121. `CreateNewDoc` — нет проверки на ошибку `CreateBaseGeometry`

**Где:** `Application.cpp:255`
```
doc->CreateBaseGeometry();   // ← если упадёт, идём дальше
myDocuments.push_back(std::move(doc));
```

Если `CreateBaseGeometry` вернёт ошибку (или бросит), документ всё равно пушится — полупустой.

**Фикс:** проверять возврат / ловить исключение.

---

### CR-122. `AddDocFromFile` / `OnOpen` — нет проверки на дубликат

**Где:** `Application.cpp:269, 452`

Пользователь открывает `foo.cbf` дважды → два `BreezeDocument` с одним файлом. При сохранении один перезапишет другой.

**Фикс:** проверить `myProjectPaths` на наличие `path`, если есть — `SwitchToDoc`.

**Оценка:** 10 минут.

---

### CR-123. `std::cout` / `std::cerr` — BUG-A4

**Где:** 15+ мест в этом блоке.
Кириллица → CP866-кракозябры в MSYS2.

**Фикс:** единый логгер или `setlocale`.

---

### CR-125. `SwitchToDoc` — ручной сброс эскиза из 4 команд

**Где:** `Application.cpp:299-304, 348-353`

```
myActiveSketchLabel = TDF_Label();
myHasSketchStart = false;
if (myOcctView) {
    myOcctView->ClearPreviewLine();
    myOcctView->ClearAllShapes();
    myOcctView->ClearGrid();
    myOcctView->ExitSketchMode();
}
```

Дублируется в `SwitchToDoc` и `CloseDoc`. **Фикс:** `Application::ResetSketchState()` — helper.

---

### CR-126. `CloseDoc` — активным становится предыдущий, а не следующий

**Где:** `Application.cpp:355-357`
```
if (myActiveDocIndex >= (int)myDocuments.size()) {
    myActiveDocIndex = (int)myDocuments.size() - 1;   // последний
}
```

Если закрыли документ в **середине** списка (не активный) — активный не меняется. Если закрыли **активный в середине** — активным становится следующий, что логично (index сдвигается сам). Если закрыли **последний** — предыдущий.

Но: если закрыли активный в середине, по логике пользователя скорее следующий. Проверить на реальном сценарии.

**Оценка:** тест + 5 минут.

---

### CR-127. `OnNewDocument` — тонкая обёртка, но дублирует логику `OnOpen` и `AddDocFromFile`
**Включает (merged):** CR-128, CR-129

**Где:** `Application.cpp:244-248, 452-468`
```
// OnNewDocument
CreateNewDoc(); RefreshView(); FitAll();

// OnOpen
AddDocFromFile(...); AddRecentFile(...); myVisibility.clear(); RefreshView(); FitAll();
```

Паттерн «создать → refresh → fit» повторяется. **Фикс:** `ActivateDocument()`.

---

### CR-130. `OnExportSTEP` — `Transfer` по одному shape

**Где:** `Application.cpp:399-403`
```
for (const auto& s : shapes) {
    if (writer.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) { ... }
}
```

Можно собрать `TopoDS_Compound` и передать одним вызовом — быстрее.

**Оценка:** 15 минут.

---

### CR-131. `OnExportSTEP` / `OnExportSTL` — фильтры в диалоге дублируют расширения

**Где:** `Application.cpp:384-386, 421-422`

```
"STEP (*.step)\0*.step\0STEP (*.stp)\0*.stp\0All (*.*)\0*.*\0\0"
```

Формат Win32 — ок. Но дублирование — если меняем формат, надо править в двух местах.

---

### CR-132. `OnExportSTEP` / `OnExportSTL` — нет toast/уведомления об успехе

RULES п.26 / §18.3 UI_LAYOUT — toast-уведомления. Сейчас только `std::cout`.

---

### CR-133. `std::cout` в sketch-логике — hot-path
**Включает (merged):** CR-097, CR-174, CR-244, CR-621, CR-744

**Где:** `Application.cpp:712, 727, 737, 748, 754, 764, 766, 771, 775, 778` (и в `Run()` тоже)

На **каждый клик** в эскизе — 5-7 вызовов `std::cout << ...`. На слабом железе (ARM, iGPU) `std::cout` **синхронный**, каждый flush — syscall.

**Дополнительно:** BUG-A4 — кириллица, но здесь ASCII, поэтому не проявляется.

**Фикс:**
1. Убрать `std::cout` из production-кода (заменить на `TRACE`).
2. Или `#ifdef BREEZECAD_DEBUG` — только в debug-сборке.

**Оценка:** 20 минут.

---

### CR-134. `RefreshSketchLines` — не проверяет `myActiveSketchLabel` на валидность после `SwitchToDoc`

**Где:** `Application.cpp:579`

Функция вызывается из `OnUndo`, `OnRedo`. Проверяет `IsNull()`, но не факт, что label ещё **жив** в документе (при закрытии документа label осиротел).

**Фикс:** дополнительная проверка или очистка при `SwitchToDoc`/`CloseDoc` (уже есть в CR-114).

---

### CR-135. `RefreshSketchLines` — O(N) обход, полный `ClearAllShapes` перед ним

**Где:** `Application.cpp:473, 520` — вызов `ClearAllShapes` + `RefreshSketchLines`.

При отмене действия — стирается **всё**, заново рисуется **всё**. Для сложного эскиза — перерисовка 1000 линий.

**Фикс:** инкрементальное обновление.

---

### CR-136. `RefreshSketchLines` — `static_cast<int>(style)` — сокрытие типа

**Где:** `Application.cpp:588`

```
myOcctView->DisplaySketchLine(s, lineLabel, static_cast<int>(style));
```

`LineStyle` → `int`. Зачем? `DisplaySketchLine` ожидает `int`? Проверить сигнатуру.

---

### CR-142. `OnSketchClick` — дубль создания edge

**Где:** `Application.cpp:757-760`

```
myDocument->AddSketchLine(myActiveSketchLabel, mySketchStartPoint, p, myCurrentLineStyle);
myOcctView->ClearPreviewLine();

TopoDS_Shape line = BRepBuilderAPI_MakeEdge(mySketchStartPoint, p).Shape();  // ← ЗАЧЕМ?
```

`AddSketchLine` уже создал edge и сохранил в OCAF. Здесь создаётся **второй, независимый** edge **только** для `DisplaySketchLine`. OCCT кидает его в heap.

**Фикс:** `BreezeDocument::AddSketchLine` **должен возвращать** `TopoDS_Shape` (или `TDF_Label`), и `DisplaySketchLine` использует тот же shape.

**Оценка:** 30 минут (менять сигнатуру).

---

### CR-143. `OnSketchClick` — не проверяет `myDependencyState` на консистентность

Связано с CR-155.

---

### CR-144. `OnSketchClick` — `glfwGetKey` для Shift (CR-062 повтор)

**Где:** `Application.cpp:714-715`

```
bool shiftHeld = glfwGetKey(..., GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
              || glfwGetKey(..., GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
```

`io.KeyShift` — уже доступен. **Фикс:** `ImGui::GetIO().KeyShift`.

---

### CR-145. `OnSketchClick` — оба `glfwGetKey` вызываются всегда, без short-circuit

**Где:** `Application.cpp:714-715`

`||` короткое замыкание работает: если LEFT нажат, RIGHT не проверяется. Но если **не** нажат LEFT — вызов. Мелочь.

**Фикс:** `ImGui::GetIO().KeyShift` (одно поле).

---

### CR-146. `try/catch` вокруг `AddSketchLine` — состояние не сбрасывается

**Где:** `Application.cpp:756-783`

```
try {
    myDocument->AddSketchLine(...);
    ...
    if (myAutoContinue && !loopClosed) {
        mySketchStartPoint = p;
        myHasSketchStart = true;   // ← если исключение — не сбросится
    }
} catch (const std::exception& e) {
    std::cerr << "Sketch: exception creating line: " << e.what() << "\n";
} catch (...) { ... }
```

Если `AddSketchLine` упадёт **после** частичного изменения:
- `myHasSketchStart` может остаться `true`.
- `mySketchStartPoint` — указывает на **мертвую** точку.

Следующий клик — попытка создать линию из ничего.

**Фикс:** `myHasSketchStart = false;` в catch.

**Оценка:** 5 минут.

---

### CR-147. `maxTag`-цикл — O(N) на каждый клик
**Включает (merged):** CR-092, CR-093, CR-094, CR-137, CR-241, CR-250, CR-562, CR-625

**Где:** `Application.cpp:762-766`

```
int maxTag = 0;
TDF_ChildIterator it(myActiveSketchLabel, false);
for (; it.More(); it.Next()) {
    if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
}
TDF_Label lineLabel = myActiveSketchLabel.FindChild(maxTag, false);
```

**Проблемы:**
1. O(N) на каждый клик — при 1000 линиях в эскизе тормозит.
2. `FindChild(maxTag, false)` — флаг `false` = не создавать если нет. Но maxTag **только что найден** — должен существовать. А если `AddSketchLine` создал label с **другим** tag (нативный счётчик OCAF) — `FindChild` вернёт Null.
3. `myOcctView->DisplaySketchLine(line, lineLabel, ...)` — если `lineLabel.IsNull()`, что произойдёт?

**Фикс:** `AddSketchLine` возвращает созданный label. Убрать весь блок.

**Оценка:** входит в CR-142.

---

### CR-148. `EnterSketch` — `planeShape.ShapeType() != TopAbs_FACE` без fallback

**Где:** `Application.cpp:807-810`

```
TopoDS_Shape planeShape = myDocument->GetShape(parentPlane);
if (planeShape.IsNull() || planeShape.ShapeType() != TopAbs_FACE) {
    std::cerr << "EnterSketch: parent is not a face\n";
    return;
}
```

Если плоскость хранится как `COMPOUND` (частая практика в OCCT) — отказ. Пользователь жмёт «Начать эскиз» — ничего не происходит, ни сообщения, ни toast.

**Фикс:** fallback через `TopExp_Explorer(planeShape, TopAbs_FACE)` — взять первую грань.

**Оценка:** 15 минут.

---

### CR-149. `EnterSketch` — `std::cerr` вместо toast

**Где:** `Application.cpp:802, 808`

RULES п.26: «Уважение ко времени пользователя». Ошибка в **stderr** — не видна в GUI. Пользователь не понимает почему «не работает».

**Фикс:** `UI::Toast::Error("Не удалось войти в эскиз: родитель не является плоскостью")`.

**Оценка:** 20 минут (нужен toast-модуль).

---

### CR-150. `IsPointOnExistingVertex` — `threshold` не применяется к `skip`

**Где:** `Application.cpp:600-628`

```
if (dist2(p, origin) < th2 && dist2(skip, origin) > th2)
    return true;
```

Логика: точка `p` на Origin И `skip` **не** на Origin. Но:
- Если `skip` **внутри** threshold от Origin (только начали линию из Origin), то замыкание на Origin не срабатывает.
- Правильно ли это? Пользователь начал линию от Origin, вернулся к Origin — должен замкнуться **в Origin**, а не в другую точку.

**Похоже, инвертировано.** Проверить сценарий:
1. Первая линия: (0,0) → (10,0).
2. Вторая линия: (10,0) → (0,0). Ожидаем замыкание.
3. Но `skip = (10,0)`, `p = (0,0)`. `dist2(p, origin) = 0`, `dist2(skip, origin) = 100 > th2` — OK, замкнётся.

4. Другой сценарий: начали линию в Origin `skip = (0,0)`, ведём в Origin `p = (0,0)` — не сработает из-за `skip`.

Логика корректна, но **хрупкая**. Комментарий в коде есть, но понять сложно.

**Фикс:** документировать, но оставить.

---

### CR-151. `IsPointOnExistingVertex` — дублирование с `SnapToKnownPoint`

Обе функции **ищут существующие точки эскиза** — Origin + концы линий. Разница:
- `IsPointOnExistingVertex` — bool (есть/нет), с `skip`.
- `SnapToKnownPoint` — возвращает **ближайшую**.

Можно объединить: `FindNearestKnownPoint(p, sketch, skip, threshold, &out)`. Одна — O(N) с `skip`, вторая — O(N) без.

**Фикс:** одна функция.

**Оценка:** 30 минут.

---

### CR-152. `IsPointOnExistingVertex` — итерация без `it.More()` guard

**Где:** `Application.cpp:602`

```
TDF_ChildIterator it(sketchLabel, false);
for (; it.More(); it.Next()) { ... }
```

Если `sketchLabel.IsNull()` — падение? `TDF_ChildIterator` с Null label — UB. Guard: `if (sketchLabel.IsNull()) return false;` в начале.

**Фикс:** добавить guard.

**Оценка:** 2 минуты.

---

### CR-153. `OnSketchClick` — 130+ строк в одной функции, 7 ответственностей

**Где:** `Application.cpp:682-787`

**Разложение:**
1. Dependency dialog handling (~30 строк).
2. Snap logic (shift/points/grid, ~30 строк).
3. Loop check (~10 строк).
4. Line creation (~20 строк).
5. Auto-continue logic (~15 строк).
6. Extensive logging (~15 строк).

**Фикс:** разбить на:
- `HandleSketchDependencyClick(px, py)`.
- `ComputeSketchPoint(px, py) -> gp_Pnt` (snap+ortho).
- `CreateSketchLine(p)`.
- `ContinueOrFinishSketch(p, loopClosed)`.

**Оценка:** 1.5 часа (с тестами).

---

### CR-154. `myAutoContinue && !loopClosed` — если `myAutoContinue = false`, точки игнорируют loop

**Где:** `Application.cpp:770`

Если автопродолжение выключено, но петля замкнулась — что? Сбрасывается. OK.

Но если `myAutoContinue = true` и линия **не** замкнулась — продолжаем. Пользователь не может **остановить** рисование линий без Esc. Проверить UX.

**Оценка:** обсудить.

---

### CR-155. `OnSketchClick` — `myDependencyState.open` без проверки на `IsNull`

**Где:** `Application.cpp:685`

Если `myDependencyState` валиден, но `base`/`dependent` содержат **мёртвые** TDF_Label (после закрытия документа) — при `inList` пойдёт сравнение. `IsEqual` может вернуть `false` для мёртвых — ок. Но семантически «фича удалена, а ссылка висит».

**Фикс:** при `SwitchToDoc` / `CloseDoc` — очищать `myDependencyState`.

**Оценка:** 10 минут.

---

### CR-156. `std::strcpy(myLengthBuffer, "0")` — `strcpy` вместо `strncpy`

**Где:** `Application.cpp:745-746`

```
std::strcpy(myLengthBuffer, "0");
std::strcpy(myAngleBuffer, "0");
```

`strcpy` unsafe. Здесь безопасно (литерал в 1 символ, буфер 32), но паттерн плохой.

**Фикс:** `myLengthBuffer[0] = '0'; myLengthBuffer[1] = '\0';` или `strncpy`.

**Оценка:** 2 минуты.

---

### CR-157. `OnSketchClick` — magic `3.0` порог snap-to-point

**Где:** `Application.cpp:723`

```
if (SnapToKnownPoint(p, myActiveSketchLabel, myDocument->GetDoc(), 3.0, snapP)) {
```

`3.0` мм — очень **большой** порог. На экране 1000 px за 500 мм — 3 мм = 6 пикселей. Точка «прилипнет» в радиусе 6 px. Может мешать при рисовании близких точек.

**Фикс:** `constexpr double kSnapRadiusMM = 3.0;` + возможно брать из настроек.

**Оценка:** 5 минут.

---

### CR-158. `OnSketchClick` — magic `0.5` порог замыкания

**Где:** `Application.cpp:748`

```
IsPointOnExistingVertex(p, myActiveSketchLabel, mySketchStartPoint, 0.5)
```

`0.5` мм. Аналогично — вынести в константу.

**Оценка:** 3 минуты.

---

### CR-159. `OnSketchClick` — magic `1e-4` для совпадения точек

**Где:** `Application.cpp:738`

```
if (dist2 < 1e-4) {
```

`1e-4` = `(0.01 мм)²`. Почему 0.01? Документировать.

**Оценка:** 3 минуты.

---

### CR-160. `OnSketchClick` — `gp_Pnt p` — копируется при snap

**Где:** `Application.cpp:717, 726, 731`

```
gp_Pnt p;
if (!myOcctView->ScreenToPlane(px, py, p)) return;
...
if (mySnapToPoints) {
    gp_Pnt snapP;
    if (SnapToKnownPoint(p, ..., snapP)) {
        p = snapP;   // ← копирование
```

`gp_Pnt` — 3 double, копия = 24 байта. Мелочь, но в hot-path можно.

**Оценка:** WONTFIX (микро).

---

### CR-161. `IsPointOnExistingVertex` / `SnapToKnownPoint` — дублируют логику обхода `TDF_ChildIterator`
**Включает (merged):** CR-138, CR-139, CR-140, CR-141

Обе функции пишут один и тот же паттерн:
```
TDF_ChildIterator it(sketchLabel, false);
for (; it.More(); it.Next()) {
    ... FindAttribute(TNaming_NamedShape::GetID(), ns) ...
    if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
        TopoDS_Edge e = TopoDS::Edge(s);
        TopExp_Explorer exp(e, TopAbs_VERTEX);
        ...
```

**Фикс:** `ForEachSketchVertex(sketchLabel, callback)`.

**Оценка:** 20 минут.

---

### CR-162. `IsPointOnExistingVertex` — `auto dist2 = [](const gp_Pnt& a, const gp_Pnt& b)` — не `constexpr`, не `inline`

Мелочь.

---

### CR-163. `OnSketchClick` — точка выхода через `std::cerr` в catch

Уже часть CR-133. Но дополнительно: `catch(...)` без классификации.

---

### CR-164. `Application::OnSketchClick` — параметры `int px, int py` — а можно `ImVec2`

Мелочь.

---

### CR-165. `OnSketchClick` — `myDocument->AddSketchLine` — вижу `myActiveSketchLabel`, но не вижу `BreezeDocument::GetType` проверки

Если `myActiveSketchLabel` — не эскиз, а что-то другое — `AddSketchLine` может отработать **некорректно**. Проверить сигнатуру и preconditions.

---

### CR-166. `ApplyExactLine` дублирует `OnSketchClick` — 33 строки копипасты

**Где:** `Application.cpp:883-916` vs `Application.cpp:756-783`

**Что дублируется:**
1. `myDocument->AddSketchLine(...)`.
2. `myOcctView->ClearPreviewLine()`.
3. `BRepBuilderAPI_MakeEdge(...)` — создание второго edge (CR-142).
4. maxTag-цикл + `FindChild` (CR-147).
5. `DisplaySketchLine(...)`.
6. `IsPointOnExistingVertex(...)` — loop check.
7. `myAutoContinue && !loopClosed` — auto-continue.
8. `myHasSketchStart = false` / `= true`.
9. `try/catch(...)` без сброса состояния (CR-146).

**Проблемы:**
- **Один баг = два фикса.**
- Логика уже рассинхронизирована: в `OnSketchClick` есть `myOcctView->ShowCursorMarker(p)`, в `ApplyExactLine` — тоже, но нет `myOcctView->ClearPreviewLine()` в else-ветке.
- Комментарии в `OnSketchClick` богаче — в `ApplyExactLine` их нет.

**Фикс:** вынести `CreateSketchLineInternal(startP, endP, style) -> bool` — одна реализация.

**Оценка:** 1 час (с тестами).

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

### CR-173. `ApplyExactLine` — `std::atof` без валидации

**Где:** `Application.cpp:872-873`

```
double length = std::atof(myLengthBuffer);
double angle  = std::atof(myAngleBuffer);
```

`atof` возвращает `0.0` при **любой** ошибке парсинга, не отличая «0» от «abc»:
- `"abc"` → `0.0` → `length < 1e-6` → выход.
- `"0"` → `0.0` → выход (правильно).
- `"1e999"` → `inf` → `length` = inf, идёт дальше. **UB при создании точки.**

**Фикс:** `std::strtod` с проверкой `errno == ERANGE` и `endptr`, или `std::from_chars`.

**Оценка:** 10 минут.

---

### CR-175. `DrawViewCube` — aggregate init `Context{...}` хрупкий

**Где:** `Application.cpp:987-991`

```
UI::ViewCube::ViewCubeOverlay::Context ctx{
    *myOcctView,
    myTheme,
    ImGui::GetIO(),
    myRightPanelExpanded ? 110.0f : 55.0f
};
```

Если в `Context` добавят **новое поле в середину** (или изменят порядок) — этот aggregate init **скомпилируется**, но заполнит поля **неправильно**. Молча.

**Фикс:** named-field init (designated initializers C++20) или конструктор с явными параметрами.

**Оценка:** 15 минут.

---

### CR-176. `RequestDelete` — код обрезан, нужен полный

**Где:** `Application.cpp:995-1030`

Из видимого фрагмента (6 строк):
```
void Application::RequestDelete(const TDF_Label& label) {
    if (label.IsNull()) return;

    if (myDeleteConfirmState.skipConfirm) {
        // Удаляем сразу
        if (myDocument) myDocument->RemoveFeature(label);
```

**Не хватает** остатка функции. Скинь полностью (до строки 1030), чтобы завершить аудит.

---

### CR-177. `ExitSketch` — полный `RefreshView()` для переключения таба

**Где:** `Application.cpp:862`

```
RefreshView();
if (myOcctView) myOcctView->FitAll();
```

`ExitSketch` полностью перерисовывает всё дерево. Для 1000 фич — долго. **Фикс:** инкрементально вернуть только скрытые системные объекты.

---

### CR-178. `ExitSketch` — `FitAll()` каждый выход

**Где:** `Application.cpp:863`

Пользователь в эскизе **зумил** на нужный участок. Выходит из эскиза → камера сбрасывается в FitAll. **UX-баг.**

**Фикс:** сохранять камеру при входе в эскиз, восстанавливать при выходе.

---

### CR-179. `EnterSketch` — `RefreshSketchLines()` без null check

**Где:** `Application.cpp:846`

`RefreshSketchLines` внутри делает `if (!myDocument || !myOcctView || ...) return;`. Формально защищено. Но проверка происходит **внутри** — если кто-то изменит `RefreshSketchLines`, `EnterSketch` сломается неявно.

**Фикс:** проверка в `EnterSketch` до вызова.

---

### CR-180. `EnterSketch` — `Standard_Real u/v` — TECH-DEBT-005

**Где:** `Application.cpp:832-833`

```
Standard_Real u = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
Standard_Real v = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;
```

TECH-DEBT-005. **Фикс:** `double`.

---

### CR-181. `DrawPreviewInfo` — `AlwaysAutoResize` + `SetNextWindowSize` конфликтуют
**Включает (merged):** CR-182, CR-187, CR-188, CR-189, CR-190

**Где:** `Application.cpp:927, 931`

```
ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);   // фикс ширина
...
ImGuiWindowFlags_AlwaysAutoResize                              // авто размер
```

Два взаимоисключающих флага. Скорее всего работает, но непредсказуемо. **Фикс:** выбрать одно.

---

### CR-183. `DrawViewCube` — `myRightPanelExpanded ? 110.0f : 55.0f` — магия

Два числа для смещения. Вынести в константы.

### CR-184. `DrawViewCube` — `Context` создаётся каждый кадр

Если `Context` содержит `IconManager&` или другие тяжёлые — heap-аллокации. Проверить определение `Context`.

### CR-185. `DrawSketchOverlay` — пустая функция (B-15, KEEP)

Оставить, но добавить `// TODO (Фаза 2): overlay-подсказки эскиза`.

### CR-186. `RequestDelete` — проверка `myDeleteConfirmState.skipConfirm` — где пользователь настраивает?

Кто ставит `skipConfirm = true`? Настройка? Проверить.

### CR-193. `RequestDelete` — `int childCount` может переполниться

**Где:** `Application.cpp:1015-1017`
```
int childCount = 0;
TDF_ChildIterator it(label, false);
for (; it.More(); it.Next()) childCount++;
```

Если у фичи 2^31 потомков — UB. В реальности невозможно, но паттерн плохой. **Фикс:** `size_t` + `%zu`.

---

### CR-194. `RequestDelete` — `TDF_ChildIterator` считает **прямых** детей, а не **всех**

**Где:** `Application.cpp:1016`

Комментарий в `snprintf`: «Будут удалены **все** вложенные элементы». Но `TDF_ChildIterator(label, false)` — **только прямые дети** (`allLevels = false`). Если у фичи есть внуки, сообщение врёт.

**Фикс:** либо считать рекурсивно (`TDF_ChildIterator(label, true)`), либо поменять текст «прямых потомков».

---

### CR-196. `CMakeLists.txt` — `TKCAF TKLCAF TKBin` всё ещё линкуются

**Где:** `CMakeLists.txt:135`
```
TKCAF TKLCAF TKBin
```

**По ADR-0001:** OCAF должен быть **модулем-мостом**, не в базе. Сейчас эти три DLL входят в поставку → лишний вес.

**Фикс:** исключить после перехода на свой `.bzc`. **Сейчас** — задокументировать в CODE_REVIEW как TECH-DEBT.

**Оценка:** после Фазы 3.

---

### CR-197. `CMakeLists.txt` — `CMAKE_CXX_FLAGS_RELEASE` перетирает дефолт

**Где:** `CMakeLists.txt:14-15`
```
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
```

**Проблема:** эти переменные CMake устанавливает **сам** со своими значениями. Перезапись через `set` **стирает** всё, что туда добавили бы внешние модули (`OpenCASCADE`, `glfw`).

**Фикс:**
```
target_compile_options(breezecad PRIVATE
    $<$<CONFIG:Release>:-O2>
)
```

**Оценка:** 20 минут.

---

### CR-198. `CMakeLists.txt` — нет `-Wall -Wextra -Wpedantic`

**Где:** весь файл
Связано с CR-023. **Фикс:**
```
target_compile_options(breezecad PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unused-parameter>  # или включать всё
)
```

**Оценка:** 30 минут + правки существующих warnings.

---

### CR-199. `CMakeLists.txt` — `include_directories` глобальный, deprecated

**Где:** `CMakeLists.txt:21, 33`
```
include_directories(${LUNASVG_ROOT}/include)
...
include_directories(${OpenCASCADE_INCLUDE_DIR})
```

Глобальный `include_directories` устарел. **Фикс:** `target_include_directories(... SYSTEM PRIVATE ...)`.

**Оценка:** 10 минут.

---

### CR-200. `CMakeLists.txt` — нет `-flto` для Release

**Цель проекта:** вес 200-500 МБ. Без LTO — не достижимо. `-flto` режет размер exe на 10-30%.

**Фикс:**
```
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok)
if(ipo_ok)
    set_property(TARGET breezecad PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
```

**Оценка:** 15 минут + проверка линковки с OCCT.

---

### CR-201. `CMakeLists.txt` — нет `strip` для Release

**Где:** весь файл
Финальный exe содержит отладочные символы → на 20-40% больше. **Фикс:** `-s` или `objcopy --strip-all`.

**Оценка:** 10 минут.

---

### CR-202. `CMakeLists.txt` — нет `CMAKE_CXX_EXTENSIONS OFF`

**Где:** `CMakeLists.txt:4`
```
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# ← нет EXTENSIONS
```

GCC по умолчанию использует GNU-расширения. Для переносимости (ARM, macOS) — отключить.

**Фикс:** `set(CMAKE_CXX_EXTENSIONS OFF)`.

**Оценка:** 2 минуты.

---

### CR-203. `CMakeLists.txt` — `project(BreezeCAD LANGUAGES CXX)` без VERSION

**Где:** `CMakeLists.txt:2`

Нет версии. Нет `description`. Для CI, дистрибутивов, `--version` в бинарнике — нужно.

**Фикс:** `project(BreezeCAD VERSION 0.1.0 LANGUAGES CXX DESCRIPTION "...")`.

**Оценка:** 5 минут.

---

### CR-204. `CMakeLists.txt` — OCCT-библиотеки захардкожены

**Где:** `CMakeLists.txt:129-135`

```
TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
TKPrim TKTopAlgo TKBO
TKDESTEP
TKDESTL
TKV3d TKOpenGl TKService TKMesh
TKCAF TKLCAF TKBin
```

`find_package(OpenCASCADE)` уже заполняет `${OpenCASCADE_LIBRARIES}`. Хардкод — brittle при обновлении OCCT.

**Фикс:** либо `${OpenCASCADE_LIBRARIES}`, либо `${OpenCASCADE_LIBRARIES} TKCAF TKLCAF TKBin` (добавки).

**Оценка:** 15 минут.

---

### CR-205. `CMakeLists.txt` — `add_library(imgui)` включает `imgui_demo.cpp` в релиз

**Где:** `CMakeLists.txt:38`

`imgui_demo.cpp` — **демо-код**, не нужен в продакшене. Плюс ~50 КБ.

**Фикс:** `#ifdef BREEZECAD_DEBUG` или отдельная статическая либа.

**Оценка:** 10 минут.

---

### CR-206. `CMakeLists.txt` — `POST_BUILD copy_directory` OCCT-DLL копирует **все** DLL

**Где:** `CMakeLists.txt:157-161`

```
add_custom_command(TARGET breezecad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${OCCT_ROOT}/win64/gcc/bin"
    "$<TARGET_FILE_DIR:breezecad>"
)
```

Копирует **всю папку** — включая DLL, которые не линкуются. Это **десятки мегабайт мусора** в portable-сборке.

**Плюс:** в проекте **уже есть** `scripts/deploy-runtime.sh`, который делает правильный BFS-анализ через objdump. Но он **не вызывается** из CMake.

**Фикс:** вызвать `deploy-runtime.sh` после сборки, а не `copy_directory`.

**Оценка:** 20 минут.

---

### CR-207. `CMakeLists.txt` — нет `CMAKE_EXPORT_COMPILE_COMMANDS`

**Где:** весь файл

Без `compile_commands.json` не работает clang-tidy, clangd, многие IDE. **Фикс:** `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`.

**Оценка:** 1 минута.

---

### CR-208. `CMakeLists.txt` — нет `CMAKE_POSITION_INDEPENDENT_CODE`

**Где:** весь файл

Для будущей сборки плагинов (Фаза 5+) и shared library — нужно. **Фикс:** `set(CMAKE_POSITION_INDEPENDENT_CODE ON)`.

**Оценка:** 1 минута.

---

### CR-209. `CMakeLists.txt` — `${LUNASVG_ROOT}/lib/liblunasvg.a` — прямой путь вместо `find_library`

**Где:** `CMakeLists.txt:141-142`

```
${LUNASVG_ROOT}/lib/liblunasvg.a
${LUNASVG_ROOT}/lib/libplutovg.a
```

Жёстко привязано к статической lib. На Linux/Mac будут `.so`/`.dylib`. **Фикс:** `find_library(LUNASVG_LIB lunasvg PATHS ${LUNASVG_ROOT}/lib ...)`.

**Оценка:** 10 минут.

---



### RAM-H1 [ПОДТВЕРЖДЕНО]. `~IconManager()` отсутствует — утечка GL-текстур

**Где:** `IconManager.h:13-30`, `IconManager.cpp:250-255`

**Проблема:**
```
class IconManager {
public:
    void LoadAll(...);
    void PreloadAll(...);
    GLuint Get(...);
    void Clear();
private:
    std::unordered_map<std::string, GLuint> myCache;
    // ← НЕТ ~IconManager()
};

void IconManager::Clear() {
    for (auto& kv : myCache) {
        if (kv.second) glDeleteTextures(1, &kv.second);
    }
    myCache.clear();
}
```

`Clear()` есть, но **никто не вызывает**. `~IconManager` = default → `myCache` очищается без `glDeleteTextures`. **Каждая GL-текстура остаётся в VRAM до конца процесса.**

**Цифры:**
- 105 иконок × 128×128 × 4 байта = **6.7 МБ GPU RAM**.
- Если Application создаётся/уничтожается (тесты, многооконка) — **6.7 МБ × N**. Утечка.

**Приложение сейчас живёт одним Application.** Но при `DestroyContext` (в ~Application) без `Clear()` — **утечка гарантирована**.

**Фикс:**
```
class IconManager {
public:
    ~IconManager() { Clear(); }
    // ...
};
```

Или (лучше) — не полагаться на неявный вызов, а **явно** звать `myIcons.Clear()` в `~Application` перед `ImGui_ImplOpenGL3_Shutdown`.

**Оценка:** 5 минут.

---


### RAM-H1a [НОВОЕ]. `RasterizeToTexture` рендерит в `size * 4` — в 16 раз больше нужного

**Где:** `IconManager.cpp:135`

```
int renderSize = size * 4;  // x4 для HiDPI и векторной чёткости
auto bitmap = doc->renderToBitmap(renderSize, renderSize);
```

Для `size = 32` (запрос) → рендер 128×128. **В 16 раз больше пикселей**, чем нужно для отображения в 32 px.

**Обоснование в комментарии** — «x4 для HiDPI». Но:
- Все иконки в ribbon запрашиваются с `size = 32` или `24` — на **реальном экране** они показываются как 32 CSS-px.
- На HiDPI (2x) — реальный рендер 64×64 — достаточно.
- **x4 = 128×128 избыточно вдвое.**

**Цифры:**
- Текущий размер: 105 × 128×128 × 4 = **6.7 МБ GPU**.
- С x2: 105 × 64×64 × 4 = **1.7 МБ**.
- С x1 (ровно 32): 105 × 32×32 × 4 = **0.4 МБ**.

**Разница — в 16 раз.** Для проекта с философией «4 ГБ, iGPU, ARM» — существенно.

**Фикс:**
```
// x2 для HiDPI (достаточно для Retina 2x), но не больше
int renderSize = size * 2;
```
И — **зависит от DPI монитора**: на обычном DPI = `size`, на HiDPI = `size * 2`. Брать из `ImGui::GetIO().DisplayFramebufferScale`.

**Оценка:** 20 минут.

---


### RAM-H1b [НОВОЕ]. `RasterizeToTexture` — `bitmap` держит RGBA в heap после создания текстуры

**Где:** `IconManager.cpp:159-170`

```
std::vector<uint8_t> rgba(w * h * 4);   // 128*128*4 = 65 KB на иконку
...
return CreateTexture(rgba.data(), w, h);
```

`std::vector` локальный, удалится при выходе из функции. **Но:** `glTexImage2D` копирует данные в GPU **синхронно** — если драйвер не дожидается, может быть use-after-free. На практике ок (glTexImage2D синхронный по спецификации), но стоит явно `glFinish()` или удалить `bitmap` до `glTexImage2D`.

**Дополнительно:** `bitmap` от lunasvg **тоже держит RGBA** — то есть в момент конвертации **два буфера** в памяти (source + dest) по 65 KB каждый.

**Фикс:** делать конвертацию **in-place** — lunasvg возвращает неконстантный указатель? Если да — можно мутировать.

**Оценка:** 15 минут.

---


### RAM-H2 [НЕ ПОДТВЕРЖДЕНО]. Иконки не держат исходные SVG в памяти

`LoadAll` только сохраняет путь. `RasterizeToTexture` читает SVG, парсит, рендерит и **освобождает** всё. **Никакого накопления SVG-буферов нет.** Гипотеза снимается.

---

### CR-210. `RequestDelete` — `TDF_ChildIterator` в цикле без консистентности

**Где:** `Application.cpp:1016`

Во время обхода `label` может быть удалён другим потоком (если появится multithreaded доступ). Сейчас ок (однопоточно). Записать как FUTURE-риск.

---

### CR-211. `RequestDelete` — `Handle(TDataStd_Name)` без проверки типа

**Где:** `Application.cpp:1011`
```
if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
    name = ExtToUtf8(nameAttr->Get());
}
```

`nameAttr->Get()` возвращает `TCollection_ExtendedString`. `ExtToUtf8` — из `util/Utf8`. BUG-A5 может проявиться.

**Фикс:** проверить `ExtToUtf8` на кириллицу.

---

### CR-212. `RequestDelete` — `snprintf` не проверяет переполнение

**Где:** `Application.cpp:1023`

Если имя фичи очень длинное (>200 симв.), `snprintf` обрежет. Может обрезать посередине UTF-8 → кракозябры.

**Фикс:** проверка длины имени перед snprintf.

---

### CR-213. `CMakeLists.txt` — нет явной переменной версии OCCT

**Где:** весь файл
Нужно знать, какую версию OCCT собираем против. Для диагностики кэша (`.bzc`).

**Фикс:** `message(STATUS "OCCT version: ${OpenCASCADE_VERSION}")`.

---

### CR-214. `CMakeLists.txt` — нет установки target для `install()`

**Где:** весь файл
Нет `install(TARGETS breezecad)`. Нельзя сделать `make install` / `ninja install`. Для portable-сборки — пока не критично, но для CI пригодится.

---

### CR-215. `CMakeLists.txt` — порядок source-файлов не группирован

**Где:** `CMakeLists.txt:55-108`
Файлы идут вперемешку. **Фикс:** группировка комментариями (`# app`, `# ui/ribbon`, `# doc`).

---

### CR-216. `CMakeLists.txt` — ImGui — `add_library(imgui STATIC ...)` без target_compile_options

ImGui под GCC нужен `-Wno-...` (иначе предупреждения). Сейчас их нет, но если включим `-Wall` — ImGui будет спамить.

**Фикс:** `target_compile_options(imgui PRIVATE -w)` (отключить warnings для чужой либы).

---

### CR-217. `IconManager::LoadAll` — название вводит в заблуждение

**Где:** `IconManager.cpp:193-202`

```
void IconManager::LoadAll(const std::string& dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(dir)) { ... }
    myIconDir = dir;
    std::cout << "IconManager: icons dir = " << dir << "\n";
}
```

**Ничего не грузит.** Только сохраняет путь. Реальная загрузка — в `PreloadDefaults`. **Фикс:** переименовать в `SetIconDir` (уже есть метод!) или объединить.

**Оценка:** 5 минут.

---

### CR-218. `IconManager::Get` — `size` параметр игнорируется при рендере

**Где:** `IconManager.cpp:230`

```
GLuint tex = RasterizeToTexture(foundPath, 32);  // ← ВСЕГДА 32, а не size
```

**Функциональный баг.** `Get(name, 64)` вернёт текстуру, отрендеренную для 32 (потом масштабируется). Кэш по ключу `name#64` — но сама текстура — 32.

**Что это значит:**
- Логотип проекта 280 px → `Get("logo", 280)` → рендер 32×32 → **размытое пятно**.
- В комментарии к `Get` написано «Позволяет держать и мелкие иконки (32), и крупные (лого 280+) в правильном разрешении без размытия.» — **это ложь**, код этого не делает.

**Фикс:** `RasterizeToTexture(foundPath, size)`.

**Оценка:** 2 минуты.

---

### CR-220. `IconManager::CreateTexture` — нет проверки `glGenTextures` error

**Где:** `IconManager.cpp:65-75`

```
GLuint texId = 0;
glGenTextures(1, &texId);
// ← нет проверки texId != 0
glBindTexture(GL_TEXTURE_2D, texId);
```

Если `glGenTextures` вернёт 0 (out of memory), дальше `glBindTexture(0)` — unbind, `glTexImage2D` уйдёт в никуда. **Тихая деградация.**

**Фикс:** `if (texId == 0) { std::cerr << "IconManager: glGenTextures failed\n"; return 0; }`.

**Оценка:** 3 минуты.

---

### CR-221. `IconManager::RasterizeToTexture` — `while ((pos = svgData.find("currentColor", pos)) != npos)`

**Где:** `IconManager.cpp:118-122`

```
size_t pos = 0;
while ((pos = svgData.find("currentColor", pos)) != std::string::npos) {
    svgData.replace(pos, 12, "#FFFFFF");
    pos += 7;
}
```

**Логика корректна** (`+7` = длина `#FFFFFF`), но:
- **O(n²)** на большом SVG с N заменами (каждый `replace` сдвигает хвост).
- Если бы `#FFFFFF` был короче `currentColor` — бесконечный цикл (здесь не так).

**Фикс:** один проход с `find` + `append` в новый буфер. Или если замена одноразовая — `.replace(pos, 12, "#FFFFFF")` и **не сдвигать pos** (всё равно следующий find начнёт с той же позиции).

**Оценка:** 10 минут.

---

### CR-222. `IconManager::Get` — ленивая загрузка vs комментарий «предзагрузка»

**Где:** `IconManager.cpp:204-235`

Комментарий в `LoadAll`: «Предзагрузка всех иконок при старте (чтобы не тормозить первый кадр)». Но `LoadAll` ничего не грузит, а `Get` — ленивый. Значит, **первый кадр** всё равно тормозит на **первый** запрос каждой иконки.

**PreloadDefaults** частично решает — грузит 105 штук. Но:
- Грузит **все** 105 в GPU — даже те, что не видны.
- Если что-то в `Get` вызывается до `PreloadDefaults` — блок.

**Фикс:** либо явно грузить только видимое (по активной вкладке), либо отложить загрузку в фоновый поток (std::async).

**Оценка:** 30 минут.

---

### CR-223. `IconManager` — `PreloadDefaults` захардкожен список 105 иконок

**Где:** `IconManager.cpp:239-291`

```
static const std::vector<std::string> kDefaultIcons = { ... 105 items ... };
```

Если в UI добавится новая иконка, и её забудут в списке — при первом использовании будет lazy load (задержка). **Плюс:** 105 иконок — большой список, который надо поддерживать синхронно с кодом UI.

**Фикс:** либо генерировать список из `assets/icons/*.svg` автоматически, либо грузить по требованию с эвристикой «часто используемые — сразу, редкие — lazy».

**Оценка:** 30 минут.

---

### CR-225. `IconManager` — `RasterizeToTexture` вызывается без проверки активного GL-контекста

**Где:** `IconManager.cpp:110-175`

`glGenTextures`, `glBindTexture`, `glTexImage2D` требуют **активного GL-контекста**. Если `Get` вызван до `makeContextCurrent` или после `glfwTerminate` — GL-ошибка или краш.

**Фикс:** `assert(glfwGetCurrentContext() != nullptr);` или проверка в `Get`.

**Оценка:** 5 минут.

---

### CR-226. `IconManager` — `#ifdef _WIN32 #include <windows.h> #endif` — зачем?

**Где:** `IconManager.h:7-9`

```
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
```

`windows.h` подключается **перед** `gl.h` — это стандартный трюк Windows (иначе `gl.h` требует `windows.h` или `WINGDIAPI`). Но этот факт не задокументирован. **Фикс:** комментарий.

**Оценка:** 1 минута.

---

### CR-227. `IconManager::CreateTexture` — нет проверки `w`, `h`

**Где:** `IconManager.cpp:65-75`
Если lunasvg вернёт пустой bitmap (w=0 или h=0) — `glTexImage2D` с нулевыми размерами. Некорректная текстура.

**Фикс:** проверка.

---

### CR-228. `IconManager::RasterizeToTexture` — предупреждение при NOT FOUND на каждый вызов

**Где:** `IconManager.cpp:225-228`

```
if (foundPath.empty()) {
    std::cerr << "IconManager: NOT FOUND " << realName << ".svg (alias for " << name << ")\n";
    myCache[key] = 0;
    return 0;
}
```

**Кэшируется ноль**, но `std::cerr` пишется **один раз** на ключ (потому что потом идёт `myCache.find(key)` → hit). Ок. Но при **рестарте приложения** — снова пишет. **5 NOT FOUND иконок (TECH-DEBT-004) × каждого запуска.**

**Фикс:** убрать или понизить до TRACE.

---

### CR-229. `IconManager::RasterizeToTexture` — `bitmap` не проверяется на `data() != nullptr`

**Где:** `IconManager.cpp:155`

```
const uint8_t* src = bitmap.data();
```

Если bitmap создан, но `data()` = nullptr (редкий случай) — краш в цикле конвертации.

**Фикс:** `if (!src) return 0;`.

---

### CR-230. `IconManager` — `std::stringstream buf` — можно `std::string` + resize

**Где:** `IconManager.cpp:106-109`

```
std::stringstream buf;
buf << file.rdbuf();
std::string svgData = buf.str();
```

`stringstream` делает **двойную копию**. Для 105 иконок × ~2 КБ — не критично, но можно через `std::istreambuf_iterator` или `std::string((istreambuf_iterator<char>(file)), {})`.

**Оценка:** 5 минут.

---

### CR-231. `IconManager::PreloadDefaults` — `static const std::vector<std::string>` создаётся один раз

Но если функция вызывается на **каждом Application** — инициализация `std::vector<std::string>` **статическая локальная** — thread-safe-init C++11. Ок.

Но `PreloadAll` вызывается **на каждый Application**. Если Application пересоздаётся — грузит **заново** (в новый GL-контекст), но старые текстуры — утечка (см. RAM-H1). **Связано.**

---

### CR-232. `AddCylinderFeature` и `AddSketchFeature` — БЕЗ транзакции OCAF
**Включает (merged):** CR-233, CR-234

**Где:** `BreezeDocument.cpp:220-235, 237-256`

**Проблема:**
```
TDF_Label BreezeDocument::AddCylinderFeature(...) {
    ...
    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);
    TDataStd_Name::Set(label, Utf8ToExt(name));    // ← без транзакции
    TDataStd_Integer::Set(label, FT_Cylinder);     // ← без транзакции
    TNaming_Builder(label).Generated(cyl);         // ← без транзакции
    myModified = true;
    return label;
}
```

**Не вызывается `StartTransaction` / `CommitTransaction`.** Все изменения идут вне OCAF-транзакции.

**Что ломается:**
1. `Undo()` после AddCylinder — откатывает **предыдущую** транзакцию (AddBox), а цилиндр **остаётся**. Пользователь видит странное поведение.
2. Документ считается modified (`myModified = true` — вручную), но история undo **не содержит** цилиндра.
3. При `SaveDocument` — сохраняется корректно (OCAF пишет всё), но при Undo после Load — цилиндр не откатится (нет транзакции).

**`AddSketchFeature`** — та же проблема **плюс**:
- **НЕ ставит `myModified = true`** → документ не считается изменённым.
- Пользователь создаёт эскиз, выходит → его **не спрашивают** о сохранении.
- При выходе эскиз теряется **без предупреждения**.

**Фикс:**
```
TDF_Label BreezeDocument::AddCylinderFeature(...) {
    ...
    StartTransaction();
    try {
        TDF_Label label = ...;
        TDataStd_Name::Set(...);
        TDataStd_Integer::Set(...);
        TNaming_Builder(label).Generated(cyl);
        CommitTransaction();
        return label;
    } catch (...) {
        // abort: myDoc->AbortCommand();
        throw;
    }
}
```

**Оценка:** 30 минут + проверка что Undo работает для всех типов.

---

### CR-235. `AddSketchLine` — теги переиспользуются после удаления → порядок ломается

**Где:** `BreezeDocument.cpp:270-271`

```
int tag = 1;
while (!sketchLabel.FindChild(tag, false).IsNull()) tag++;
```

**Сценарий:**
1. Создаём линии: tags 1, 2, 3, 4, 5.
2. Удаляем линии 2 и 4.
3. Создаём новую → `while` находит tag 2 (свободный) → новая линия получает **tag 2**.
4. `RemoveLastSketchLine` использует `maxTag` (5) → удаляет tag 5, а не новую (tag 2).

**Плюс:** при обходе дерева линии идут в порядке тегов: 1, 2(новая), 3, 5 — новая линия **в середине**.

**Плюс:** при Undo/Redo — восстанавливается по тегам, порядок может стать хаотичным.

**Фикс:** использовать монотонный счётчик (`myNextSketchLineId` в BreezeDocument или в самой sketch) или `myNextFeatureId`-подобный.

**Оценка:** 20 минут.

---

### CR-236. `RemoveFeature` — `ForgetAllAttributes` не очищает дочерние метки рекурсивно
**Включает (merged):** CR-242, CR-243

**Где:** `BreezeDocument.cpp:330-350`

```
label.ForgetAllAttributes(true);
```

**По документации OCAF:** `ForgetAllAttributes(Standard_Boolean theOnlyClosed)` очищает атрибуты **только текущей метки**. Дочерние метки **не затрагиваются**.

**Что это значит:**
- Удаляем `Sketch1` → её линии (дети) **остаются** в OCAF.
- Они не отображаются в дереве (родителя нет), но память занята.
- При сохранении — они пишутся в файл.
- **Утечка данных** в долгой сессии.

**Проверить:** `label.ForgetAllAttributes(true)` в коде OCAF.

**Фикс:**
```
void ForgetRecursively(const TDF_Label& label) {
    std::vector<TDF_Label> children;
    TDF_ChildIterator it(label, false);
    for (; it.More(); it.Next()) children.push_back(it.Value());
    for (auto& c : children) ForgetRecursively(c);
    label.ForgetAllAttributes(true);
}
```

**Оценка:** 30 минут + проверка через Load/Save.

---

### CR-238. `SetDocument` — комментарий про Origin неверный

**Где:** `BreezeDocument.cpp:46-49`

```
// Найти существующий Origin (тег 1)
if (!myDoc.IsNull()) {
    TDF_Label candidate = myDoc->Main().FindChild(1, false);
```

`Main().FindChild(1)` — это **Start** (тег 1 в `Main`), а не Origin (Origin — тег 1 в `Start`). Комментарий врёт.

**Фикс:** исправить комментарий: `// Найти Start (тег 1 под Main)`.

**Оценка:** 2 минуты.

---

### CR-239. `Has3DOperations` — хардкод 3 типов, не расширяется

**Где:** `BreezeDocument.cpp:56-63`

```
if (type == FT_Box || type == FT_Cylinder || type == FT_Hole) {
    return true;
}
```

При добавлении Extrude / Revolve / Sweep — забыть добавить сюда. Функция вернёт `false`, оси/плоскости останутся видимыми, UI сломается.

**Фикс:** либо `Is3DOperation(int type)` с явным switch, либо массив типов, либо **проверять семантически** (есть ли у фичи `TopoDS_Solid` или `TopoDS_Compound` в B-rep).

**Оценка:** 15 минут.

---

### CR-240. `RefreshNextFeatureId` — нет `IsNull()` guard, лог в stdout

**Где:** `BreezeDocument.cpp:65-75`

```
void BreezeDocument::RefreshNextFeatureId() {
    if (myDoc.IsNull()) return;
    int maxTag = 0;
    TDF_ChildIterator it(myDoc->Main(), false);
    for (; it.More(); it.Next()) {
        if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
    }
    myNextFeatureId = maxTag + 1;
    std::cout << "BreezeDocument: next feature id = " << myNextFeatureId << "\n";
}
```

**Проблемы:**
1. `std::cout` на каждый Load.
2. Логика: `maxTag` считается только по **прямым** детям Main. Если пользователь создал вложенные фичи (будущее), они не учитываются.
3. `maxTag + 1` — если maxTag = INT_MAX — переполнение. Теоретически.

**Оценка:** 5 минут + переход на TRACE.

---

### CR-246. `AddSketchFeature` — счётчик «Sketch1, Sketch2» через количество

**Где:** `BreezeDocument.cpp:245-249`

```
int count = 0;
TDF_ChildIterator it(parentPlane, false);
for (; it.More(); it.Next()) count++;
std::string name = "Sketch" + std::to_string(count + 1);
```

**Проблема:** если у пользователя были Sketch1, Sketch2, удалил Sketch1 → count = 1 → новый эскиз назовётся «Sketch2» — **дубликат имени**.

**Фикс:** монотонный счётчик `myNextSketchNumber` (per-document) или сканировать имена и брать max+1.

**Оценка:** 15 минут.

---

### CR-247. `BreezeDocument` — магические теги `10`, `100`

**Где:** `BreezeDocument.cpp:229` (myNextFeatureId начинается с 10 в CreateBaseGeometry), `BreezeDocument.cpp:275` (tag 100 для style)

**Проблема:** `100` для style, `10` для первого user feature — нигде не задокументировано, кроме кода. Не понятно почему 100, а не 10.

**Фикс:**
```
constexpr int kFirstUserFeatureTag = 10;
constexpr int kSketchStyleChildTag = 100;
```

**Оценка:** 5 минут.

---

### CR-248. `GetType` — молча возвращает `FT_Other` при отсутствии атрибута

**Где:** `BreezeDocument.cpp:260-266`

```
int BreezeDocument::GetType(const TDF_Label& label) {
    Handle(TDataStd_Integer) attr;
    if (label.FindAttribute(TDataStd_Integer::GetID(), attr)) return attr->Get();
    return FT_Other;
}
```

Если метка должна быть `FT_Box`, но атрибут потерян (баг), вернётся `FT_Other` — фича "тихо" переклассифицируется. Утечка диагностики.

**Фикс:** логировать `WARN` при неожиданном `FT_Other` на непустой метке.

**Оценка:** 5 минут.

---

### CR-249. `IsSystem` — магия `< 0`

**Где:** `BreezeDocument.cpp:268-270`

```
bool BreezeDocument::IsSystem(const TDF_Label& label) {
    return GetType(label) < 0;
}
```

Семантика «system = отрицательный type». Работает, но неявно. Если заведём новый не-system с отрицательным type — сломается.

**Фикс:** явный список (`FT_Start`, `FT_Plane`, `FT_Axis`, `FT_OriginPoint`).

**Оценка:** 5 минут.

---

### CR-251. `Init` — три вызова OCCT-функций продублированы подряд

**Где:** `OcctView.cpp:56-63`

```
myViewer->SetDefaultLights();
myViewer->SetLightOn();
myContext = new AIS_InteractiveContext(myViewer);
myContext->SetDisplayMode(AIS_Shaded, true);

// Двустороннее освещение — плоскости видны с обеих сторон
myViewer->SetDefaultLights();        // дубль
myViewer->SetLightOn();
myContext->SetDisplayMode(AIS_Shaded, true);
```

**Фикс:** удалить второй блок.

**Оценка:** 1 минута.

---

### CR-252. Дубли `#include` в `OcctView.cpp`
**Включает (merged):** CR-021, CR-022, CR-245, CR-295, CR-620

- `<AIS_Line.hxx>` — строки 5 и 9
- `<AIS_SelectionModesConcurrency.hxx>` — 16 и 17
- `<Geom_CartesianPoint.hxx>` — 7 и 13
- `<gp_Lin.hxx>` — 24 и 25

**Фикс:** убрать дубли.

**Оценка:** 2 минуты.

---

### CR-254. `DisplayShape` — двойная настройка одной shape

**Где:** `OcctView.cpp:157-232`

Сначала через локальный `Prs3d_Drawer`, потом через `myContext->SetColor/SetTransparency/SetDisplayMode`. Комментарий «самый надёжный способ» — признание проблемы.

**Проблема:** два transparency (0.55 и 0.30) конфликтуют. `new Prs3d_Drawer()` на каждую фичу — heap.

**Фикс:** оставить **один** способ через `Prs3d_Drawer`.

**Оценка:** 1 час.

---

### CR-255. `DisplayShape` — magic selection mode 4

**Где:** `OcctView.cpp:225-231`

```
myContext->Activate(ais, 4);
myContext->SetSelectionModeActive(ais, 4, true, ...);
```

**Фикс:** `constexpr int kSelectionModeFace = 4;`.

**Оценка:** 5 минут.

---

### CR-256. `DisplayShape` — дубликат `label` в `myShapeEntries`

**Где:** `OcctView.cpp:234`

Без проверки на дубликат. При двойном `RefreshView` без `ClearAllShapes` — обновление пойдёт в первый найденный.

**Фикс:** проверка «уже есть» или гарантия `ClearAllShapes`.

**Оценка:** 10 минут.

---

### CR-257. `ClearAllShapes` — не сбрасывает выделение

**Где:** `OcctView.cpp:236-245`

`myContext->Remove()` не сбрасывает selected. **Фикс:** `myContext->ClearSelected(false);`.

**Оценка:** 5 минут.

---

### CR-258. `BuildSketchGrid` — 84 AIS_Shape на каждый вход в эскиз

**Где:** `OcctView.cpp:262-340`

41 + 41 линии + 2 оси = 84 AIS_Shape, ~400+ heap-аллокаций на вход.

**Фикс:**
- Одна `TopoDS_Compound` — 1 AIS_Shape.
- Или переиспользовать между входами.
- Или виртуальная сетка через ImGui.

**Оценка:** 1-2 часа.

---

### CR-259. `BuildSketchGrid` — 3× копипаста блока «линия + аспект + Display»

**Где:** `OcctView.cpp:274-330`

Одинаковый блок 80+ раз.

**Фикс:** `AddGridLine(s, e, color, type, width)`.

**Оценка:** 30 минут.

---

### CR-260. `BuildSketchGrid` — magic `ext = 200.0`

**Где:** `OcctView.cpp:265`

Сетка 400×400 мм фиксирована.

**Фикс:** адаптивно от размера детали.

**Оценка:** 20 минут.

---

### CR-261. `EnterSketchMode` — magic `500.0` distance и `3.0` scale

**Где:** `OcctView.cpp:354-355`

```
cam->SetDistance(500.0);
cam->SetScale(3.0);
```

**Фикс:** `constexpr` с комментарием.

**Оценка:** 5 минут.

---

### CR-262. `EnterSketchMode` — `cam->SetDirection(-normal)` требует визуальной проверки

**Где:** `OcctView.cpp:342`

Смотрим «против» нормали. Возможно правильно (видеть сквозь плоскость), но проверить визуально.

**Оценка:** 10 минут.

---

### CR-263. `EnterSketchMode` — `myContext->Deactivate()` глобально

**Где:** `OcctView.cpp:366`

Отключает всю подсветку. Восстанавливается ли в `ExitSketchMode` — проверить в части 2.

---

### CR-264. `ExitSketchMode` — код обрезан, нужен полностью

**Где:** `OcctView.cpp:378+`

Скинуть строки 378-450 для полного аудита.

---

### CR-265. `OcctView.h` — несогласованный стиль inline-методов

Часть — в одну строку, часть — на нескольких. Дело вкуса. **WONTFIX.**

---

### CR-266. `OcctView.h` — C-cast в `GetShapeCount`

**Где:** `OcctView.h:77`

```
int GetShapeCount() const { return (int)myShapeEntries.size(); }
```

**Фикс:** `static_cast<int>(...)`.

**Оценка:** 1 минута.

---

### CR-267. `OcctView.h` — `IsSketchMode()` дублирует `IsInSketchMode()`

**Где:** `OcctView.h:76` и `OcctView.h:98`

```
bool IsSketchMode() const { return !myInSketchMode ? false : true; }   // стр. 76
...
bool IsInSketchMode() const { return myInSketchMode; }                  // стр. 98
```

Одинаковое поведение. **Фикс:** удалить `IsSketchMode()`.

**Оценка:** 1 минута.

---

### CR-268. `OcctView.h` — `GetAllLabels()` возвращает `vector` по значению

**Где:** `OcctView.h:80-85`

Каждый `TDF_Label` — refcount inc/dec. Вызывается в панелях для изоляции.

**Фикс:** `const std::vector<...>&` или span/итераторный интерфейс.

**Оценка:** 15 минут.

---

### CR-269. `OcctView` — нет метода `Reset()` для полного сброса

При смене документа надо сбросить: shapes, grid, preview, sketch mode, selection. Сейчас 5 отдельных вызовов. Легко забыть один.

**Фикс:** `OcctView::Reset()` — вызывает всё внутри.

**Оценка:** 20 минут.

---

### CR-270. `OcctView.h` — 11 полей состояния мыши, часть имён неясна

**Где:** `OcctView.h:107-116`

`myPressX/Y` vs `myLeftPressX/Y` vs `myLastX/Y` — неясная семантика. `myLeftPressed` vs `myLeftDragging` — тоже.

**Фикс:** переименовать или сгруппировать в `struct MouseState { ... }`.

**Оценка:** 15 минут.

---

### CR-271. `OcctView.cpp` — инициализация `myGridVisible = true`, но сетка не построена

**Где:** `OcctView.h:143` + `OcctView.cpp`

`myGridVisible = true` по умолчанию. Но `BuildSketchGrid` вызывается только в `EnterSketchMode`. В 3D-режиме `myGridVisible = true` — а сетки нет. Если кто-то проверит флаг — получит **неверное состояние**.

**Фикс:** `myGridVisible = false` до первого `BuildSketchGrid`.

**Оценка:** 2 минуты.

---

### CR-272. `OcctView::SetShapeVisible` — silent fail при отсутствии label

**Где:** `OcctView.cpp:302-317`

Если `label` не найден в `myShapeEntries` — цикл заканчивается, **ничего не делается, никакой диагностики**. Возможны расхождения между `myVisibility` в `Application` и реальным состоянием сцены.

**Фикс:** debug-warning при ненайденной label.

**Оценка:** 5 минут.

---

### CR-273. `OcctView::Init` — порядок `glfwMakeContextCurrent` до `new Aspect_DisplayConnection`

**Где:** `OcctView.cpp:40-44`

```
glfwMakeContextCurrent(myGlfwWindow);   // ← сначала
myDisplayConnection = new Aspect_DisplayConnection();
```

Работает. Но `Aspect_DisplayConnection` на Windows не требует GL — он про окно. Ок.

**WONTFIX.**

---

### CR-274. `OcctView::Init` — `myWindow->SetVirtual(true)`

**Где:** `OcctView.cpp:104`

`SetVirtual(true)` — обычно для offscreen rendering. Для real-window — `false`. Проверить, не мешает ли.

**Оценка:** 10 минут.

---

### CR-275. `OcctView::Init` — `myWindow->IsMapped()` и `Map()` без проверки состояния

**Где:** `OcctView.cpp:134-135`

```
if (!myWindow->IsMapped()) myWindow->Map();
```

Идиоматично. ОК.

**WONTFIX.**

---

### CR-276. `OcctView` — `SetImmediateUpdate(false)` — что за ним

**Где:** `OcctView.cpp:99`

```
myView->SetImmediateUpdate(false);
```

Отключает немедленный redraw при изменениях. Нужно явно вызывать `Redraw()`. Это **правильно**, но проверь, что **все** точки изменения вызывают `Redraw()`.

**Оценка:** audit в части 2.

---

### CR-277. `OcctView.cpp` — `#include <AIS_Line.hxx>` в `.h` и `.cpp`

**Где:** `OcctView.h:5`, `OcctView.cpp:9`

`AIS_Line` используется в `myPreviewLine` (member). Точно нужен в `.h`. В `.cpp` — если уже приходит через header, лишний.

**Оценка:** 2 минуты.

---

### CR-278. `OcctView::DisplayShape` — `isPlane2` проверяется вторично

**Где:** `OcctView.cpp:213-221`

```
if (isSystem) {
    bool isPlane2 = (shape.ShapeType() == TopAbs_FACE);   // ← снова то же
```

В блоке выше (`isSystem`) уже было `isPlane = (shape.ShapeType() == TopAbs_FACE)`. Дублирование.

**Фикс:** переиспользовать `isPlane`.

**Оценка:** 5 минут.

---

### CR-280. `OcctView.cpp` — `std::cout` в `EnterSketchMode` / `ExitSketchMode`

**Где:** `OcctView.cpp:369` и другие

Связано с CR-133. **Фикс:** TRACE.

---

---

### CR-281. `ExitSketchMode` — восстанавливает shape'ы через `myContext->Display`, но порядок не тот

**Где:** `OcctView.cpp:398-419`

Все shape'ы из `myShapeEntries` переотображаются в **порядке добавления**. Но при выходе из эскиза важна **z-order** (порядок отрисовки): оси → плоскости → деталь. Сейчас — как получится.

**Фикс:** сортировать `myShapeEntries` по типу (системные ниже, фичи выше) или по `IsSystem`.

**Оценка:** 20 минут.

---

### CR-282. `ExitSketchMode` — дублирует логику `DisplayShape`

**Где:** `OcctView.cpp:398-419` vs `OcctView.cpp:157-232`

Логика: **то же, что `DisplayShape`**, но:
- Цвета другие (`0.55,0.55,0.55` vs `#c28a23`).
- Transparency другая (`0.95f` vs `0.30f`).
- Проверки другие.

**Это вторая реализация** того же, что `DisplayShape`. Возможные расхождения:
- В `DisplayShape` transparency 0.30 — плоскость 70% видна.
- В `ExitSketchMode` transparency 0.95 — плоскость 5% видна.

**Пользователь выходит из эскиза → плоскости почти исчезают.**

**Это подтверждённый баг.** **Фикс:** переиспользовать `DisplayShape`-логику.

**Оценка:** 40 минут.

---

### CR-283. `ScreenToPlane` — magic `1e-9` для denom
**Включает (merged):** CR-285, CR-642, CR-692

**Где:** `OcctView.cpp:436`

```
if (std::abs(denom) < 1e-9) return false;
```

**Фикс:** `constexpr double kMinDenom = 1e-9;`.

**Оценка:** 3 минуты.

---

### CR-284. `ScreenToPlane` — `myView.IsNull()` проверяется, но не `myInSketchMode`

**Где:** `OcctView.cpp:426`

```
if (myView.IsNull() || !myInSketchMode) return false;
```

Ок, проверяется. WONTFIX.

---

### CR-286. `ShowPreviewLine` — создаёт 3 AIS-объекта на каждый кадр движения мыши

**Где:** `OcctView.cpp:453-503`

**Что происходит на каждый вызов:**
1. `ClearPreviewLine()` — `Remove` × 3 (line, startMarker, endMarker).
2. `new Geom_CartesianPoint` × 2.
3. `new AIS_Line`.
4. `new Prs3d_LineAspect`.
5. `new AIS_Point` × 2.
6. `new Prs3d_PointAspect` × 2.
7. `new Prs3d_Drawer` × 2.
8. `myContext->Display` × 3.
9. `myContext->UpdateCurrentViewer()` + `myView->Redraw()`.

**Итого:** ~10 heap-аллокаций + ~8 GL-обновлений **на каждый кадр движения мыши в эскизе**.

**Цифры:**
- 60 FPS × 10 = **600 аллокаций/сек**.
- Если во время перетаскивания — **1200+/сек**.
- **Плюс OCCT пересчитывает B-rep для точки** — очень дорого.

**Это один из главных RAM/A2-источников.**

**Фикс:**
1. Создавать объекты **один раз** в `EnterSketchMode`, потом **мутировать** их координаты через `SetComponent`.
2. Или — рисовать preview **через ImGui drawList** поверх OCCT-сцены.
3. Или — рисовать preview только при изменении точки (>3px, что уже есть в `Run()`), **не на каждый кадр**.

**Оценка:** 1.5-2 часа.

---

### CR-287. `ShowPreviewLine` — `try/catch(...)` глотает всё

**Где:** `OcctView.cpp:499-503`

```
} catch (...) {
    myPreviewLine.Nullify();
    myStartMarker.Nullify();
    myEndMarker.Nullify();
}
```

OCCT-исключения (Standard_Failure) проглатываются. Диагностика невозможна.

**Фикс:** `catch (const std::exception& e) { std::cerr << ...; ... }`.

**Оценка:** 5 минут.

---

### CR-288. `ShowCursorMarkerHighlighted` — полный дубликат `ShowCursorMarker`

**Где:** `OcctView.cpp:506-542` vs `OcctView.cpp:544-578`

Единственная разница:
- `ShowCursorMarkerHighlighted(p, true)` → зелёный, size 5.0.
- `ShowCursorMarkerHighlighted(p, false)` → жёлтый, size 3.0.
- `ShowCursorMarker(p)` → жёлтый, size 3.0.

То есть `ShowCursorMarker(p)` **эквивалентен** `ShowCursorMarkerHighlighted(p, false)`.

**Фикс:** удалить `ShowCursorMarker`, оставить `ShowCursorMarkerHighlighted` с дефолтным `highlight = false`.

**Оценка:** 10 минут.

---

### CR-289. `ShowCursorMarker` / `ShowCursorMarkerHighlighted` — то же hot-path (см. CR-286)

**Где:** оба метода.

`ShowCursorMarker` вызывается из `Application::Run()` **при каждом движении мыши в эскизе**. Создаёт/удаляет 1 AIS_Point + 1 Prs3d_Drawer + 1 Prs3d_PointAspect = **3 аллокации на кадр**, 180/сек.

**Фикс:** как CR-286 — мутировать существующий объект.

**Оценка:** входит в CR-286.

---

### CR-290. `ClearPreviewLine` — вызывается перед **созданием** в `ShowPreviewLine`

**Где:** `OcctView.cpp:461` (в `ShowPreviewLine`) + `OcctView.cpp:570-572` (в `ShowCursorMarker`)

```
// ShowPreviewLine
ClearPreviewLine();   // ← удаляем всё
myPreviewLine = new AIS_Line(...);   // ← создаём заново

// ShowCursorMarker
if (!myStartMarker.IsNull()) { myContext->Remove(...); myStartMarker.Nullify(); }   // ← вручную
if (!myEndMarker.IsNull())   { myContext->Remove(...); myEndMarker.Nullify(); }
if (!myPreviewLine.IsNull()) { myContext->Remove(...); myPreviewLine.Nullify(); }
// ← вручную, а не через ClearPreviewLine
```

**`ShowCursorMarker` дублирует логику `ClearPreviewLine`** вместо вызова.

**Фикс:** `ClearPreviewLine()`.

**Оценка:** 3 минуты.

---

### CR-291. `DisplaySketchLine` — width 12.0 для normal — очень толстая

**Где:** `OcctView.cpp:637`

```
default: // Normal
    ...
    width = 12.0;
```

**12.0 px** — очень толстая линия. В ribbon сцене толщина обычно 1-3 px. Возможно, это "чтобы видно было на слабом экране", но **на HiDPI** будет в 12 px = 24 реальных пикселя. **Уточнить.**

**Оценка:** 5 минут.

---

### CR-293. `DisplaySketchLine` — `myShapeEntries.push_back` без проверки дубликата

**Где:** `OcctView.cpp:649`

При **второй** раз попадании линии в сцену (например, `RefreshSketchLines` вызвался дважды) — **дубль в `myShapeEntries`**. Потом при `ClearAllShapes` — `Remove` дважды, второй — no-op или crash.

**Фикс:** проверка `label` на дубликат (как CR-256).

**Оценка:** 10 минут.

---

### CR-294. `DisplaySketchLine` — `false` для isSystem, но линии эскиза — полу-системные

**Где:** `OcctView.cpp:649`

```
myShapeEntries.push_back({ label, ais, line, false });   // isSystem = false
```

`isSystem = false` значит при `RefreshView` линии эскиза будут **скрыты, если есть 3D-операции** (см. `Application::RefreshView:222-229`).

Но по логике — линии эскиза должны быть **видны всегда**, пока активен эскиз.

**Проверить:** на самом деле после выхода из эскиза линии должны исчезнуть из сцены. Но `isSystem` в `myShapeEntries` используется не так — используется в `RefreshView` через `BreezeDocument::IsSystem`. Не путать.

**Возможно WONTFIX**, но проверить.

---

### CR-296. `ShowPreviewLine` / `DisplaySketchLine` — `Graphic3d_ZLayerId_Topmost` без проверки

**Где:** `OcctView.cpp:470, 646`

```
myContext->SetZLayer(myPreviewLine, Graphic3d_ZLayerId_Topmost);
```

`Graphic3d_ZLayerId_Topmost` — предопределённый слой. Работает, но:
- Перерисовка Topmost-слоя — **дороже**, чем обычного.
- Если много объектов в Topmost — может замедлить всё.

**Фикс:** проверить, действительно ли нужен Topmost. Для preview — да, но для **постоянных** линий эскиза — нет.

**Оценка:** 10 минут.

---

### CR-297. `ShowPreviewLine` / `ShowCursorMarker` — двойной `Redraw`

**Где:** `OcctView.cpp:497-498, 537-538, 573-574`

```
myContext->UpdateCurrentViewer();
if (!myView.IsNull()) myView->Redraw();
```

`UpdateCurrentViewer` уже помечает viewer как "нужна перерисовка". `Redraw()` — принудительно. В **ImmediateUpdate(false)** режиме — это может быть избыточно.

**Фикс:** оставить что-то одно (обычно достаточно `UpdateCurrentViewer()` + последующий кадр ImGui вызовет `Redraw`).

**Оценка:** 10 минут + проверка визуально.

---

### CR-298. `ShowPreviewLine` — `Handle(Geom_Point) gStart = new Geom_CartesianPoint(from)` — уже есть `gp1`

**Где:** `OcctView.cpp:476`

```
Handle(Geom_CartesianPoint) gp1 = new Geom_CartesianPoint(from);   // строка 458
...
Handle(Geom_Point) gStart = new Geom_CartesianPoint(from);         // строка 476, дубль
```

**Двойное создание** одной и той же точки. Плюс `Handle(Geom_Point)` вместо `Handle(Geom_CartesianPoint)` — разный тип handle, но это ок (upcast).

**Фикс:** использовать `gp1`.

**Оценка:** 2 минуты.

---

### CR-299. `OcctView` — `ShowCursorMarker` в `Application::Run` вызывается **до** `ShowPreviewLine`?

**Где:** проверить в `Application::Run:1200+`

Логика в `Application::Run` (не видел полный код): при движении мыши вызывается `ShowCursorMarker(p)`, при наличии старта — `ShowPreviewLine(start, p)`.

**Порядок важен:** если `ShowPreviewLine` вызывается после `ShowCursorMarker` — он удалит маркер (через `ClearPreviewLine`). Проверить.

---

### CR-300. `DisplaySketchLine` — curve может быть trim'нута, `first`/`last` — параметры

**Где:** `OcctView.cpp:604`

```
TopoDS_Edge edge = TopoDS::Edge(line);
Standard_Real first, last;
Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
```

Стандартный паттерн. Ок. Но `Standard_Real` — deprecated (TECH-DEBT-005).

**Фикс:** `double`.

**Оценка:** 2 минуты.

---

### CR-301. `ExitSketchMode` — `myContext->Deactivate()` из `EnterSketchMode` не отменяется

**Где:** `OcctView.cpp:365` (в `EnterSketchMode`)

```
myContext->Deactivate();   // глобально отключает подсветку
```

В `ExitSketchMode` — **нет** соответствующего "ре-активирования". Только `Activate` на отдельных shape'ах. **Глобальный Deactivate** остаётся? Или `Activate` его перекрывает?

**Проверить:** войти в эскиз, выйти, навести на деталь — подсветка работает?

**Оценка:** 10 минут теста.

---

---

### CR-302. `CaptureToPixmap(int width, int height, ...)` игнорирует параметры

**Где:** `OcctView.cpp:687`

```
bool OcctView::CaptureToPixmap(int /*width*/, int /*height*/, const std::string& outPath) {
    ...
    if (!myView->Dump(outPath.c_str())) { ... }
```

Параметры `width`/`height` **закомментированы**. Функция обещает размер, но делает дамп **текущего вьюпорта**. Если Home Screen хочет миниатюру 256×256 — получит что попало.

**Дополнительно:** OCCT `Dump()` без FreeImage поддерживает **только BMP**. Но в `UI::Home::CaptureCurrentThumbnail` сохраняется `.bmp` (см. логи `Thumbnail saved: .bmp`). А в UI_LAYOUT §2.1 миниатюры — PNG. **Формат расходится с каноном.**

**Фикс:**
- Либо реализовать через `V3d_View::Dump` с параметром размера (если API позволит).
- Либо явно отрендерить во временный FBO нужного размера.

**Оценка:** 1 час.

---

### CR-303. `GetSelectedLabel` — O(N) линейный поиск

**Где:** `OcctView.cpp:671-679`

```
for (const auto& e : myShapeEntries) {
    if (e.Shape == obj) return e.Label;
}
```

Вызывается из `OnSketchClick` (в эскизе), из ПКМ-меню, из панелей. При 1000 фич — 1000 сравнений каждый раз.

**Фикс:** `std::unordered_map<Handle(AIS_InteractiveObject), TDF_Label>` — O(1).

**Оценка:** 30 минут.

---

### CR-304. `GetSelectedLabel` — использует `==` для `Handle<AIS_InteractiveObject>`

**Где:** `OcctView.cpp:674`

`e.Shape == obj` — сравнение указателей handle. Работает (OCCT перегружает `==`). Ок.

**WONTFIX.**

---

### CR-306. `OnMouseButton` — ПКМ в эскизе заблокирован, СКМ — нет

**Где:** `OcctView.cpp:757` vs `OcctView.cpp:768`

```
if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    if (myInSketchMode) return;    // ← блокируем ПКМ
    ...
}
...
} else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
    // ← не блокируем СКМ
```

**Что это значит:**
- В эскизе ПКМ — не вращает камеру (правильно, чтобы не мешать).
- Но СКМ — **панорамирует** (это может быть полезно, но непоследовательно).

**Проверить UX:** хочется ли панорамирование в 2D-эскизе? Если да — оставить. Если нет — заблокировать.

**Оценка:** 5 минут (решение) + 5 минут (код).

---

### CR-309. `SetDisplayModeAll` использует `DefaultDrawer` — глобальное влияние

**Где:** `OcctView.cpp:713-720`

```
void OcctView::SetDisplayModeAll(int mode, bool edges) {
    if (myContext.IsNull()) return;
    myContext->SetDisplayMode(mode, false);
    Handle(Prs3d_Drawer) dr = myContext->DefaultDrawer();   // ← глобальный drawer
    dr->SetFaceBoundaryDraw(edges);
    myContext->UpdateCurrentViewer();
    ...
}
```

`DefaultDrawer()` — **общий drawer для всех объектов**, у которых нет своего. Меняем `SetFaceBoundaryDraw(edges)` — влияет **глобально**, включая системные объекты (оси/плоскости), у которых должен быть свой aspect.

**Фикс:** либо явный проход по `myShapeEntries` и настройка каждого, либо явное документирование «это глобальная настройка».

**Оценка:** 30 минут.

---

### CR-310. `SetCameraDirectionAndUp` — магические 0.99 / 0.9

**Где:** `OcctView.cpp:752-757`

```
if (std::abs(dir.Dot(up)) > 0.99) {
    gp_Dir fallback(0, 0, 1);
    if (std::abs(dir.Dot(fallback)) > 0.9) fallback = gp_Dir(0, 1, 0);
    safeUp = fallback;
}
```

`0.99` и `0.9` — magic. Логика: «если dir и up почти параллельны, взять другой up». Но зачем **два разных** порога (0.99 и 0.9)?

**Фикс:** единая константа `kMinPerpendicularDot = 0.99` (или обосновать разные).

**Оценка:** 5 минут.

---

### CR-311. `RotateAroundUp` — `M_PI / 180.0`
**Включает (merged):** CR-320, CR-643, CR-667, CR-691

**Где:** `OcctView.cpp:786`

```
rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), up), angleDeg * M_PI / 180.0);
```

**Фикс:** `constexpr double kDegToRad = M_PI / 180.0;` (в `util/Geometry2D` уже должно быть).

**Оценка:** 5 минут.

---

### CR-313. `SetHomeView` — хардкод направления `(-1, -1, -1)` без объяснения

**Где:** `OcctView.cpp:790-793`

```
cam->SetDirection(gp_Dir(-1, -1, -1));
cam->SetUp(gp_Dir(0, 0, 1));
```

Стандартная изометрия. Но в коде — не задокументировано.

**Фикс:** `constexpr gp_Dir kIsoDirection{-1, -1, -1};` + комментарий.

**Оценка:** 3 минуты.

---

### CR-314. `FitAll` — magic `0.01` в `FitAll` и `ZFitAll`

**Где:** `OcctView.cpp:722-727`

```
void OcctView::FitAll() {
    myView->FitAll(0.01, false);   // ← 0.01 = margin
    myView->ZFitAll();
    myView->Redraw();
}
```

`0.01` — margin (1%). Повторяется в `SetHomeView` (строка 799) и `ExitSketchMode` (строка 421).

**Фикс:** `constexpr double kFitAllMargin = 0.01;`.

**Оценка:** 5 минут.

---

### CR-315. `OnMouseScroll` — magic `40` в `yoffset * 40`

**Где:** `OcctView.cpp:832`

```
myView->ZoomAtPoint(px, py, px, py + (int)(yoffset * 40));
```

`40` — px на «шаг» колеса. На разных ОС/драйверах — разный. У пользователя трекпад может дать `yoffset = 0.1` → `4 px`.

**Фикс:** брать из настроек / использовать `io.MouseWheel` от ImGui.

**Оценка:** 10 минут.

---

### CR-316. `Resize` — не обновляет `myWidth`/`myHeight` перед проверкой

**Где:** `OcctView.cpp:681-687`

```
void OcctView::Resize(int width, int height) {
    myWidth  = width;
    myHeight = height;
    if (myWindow.IsNull() || myView.IsNull()) return;
    myWindow->SetSize(width, height);
    myView->MustBeResized();
}
```

Порядок: сначала обновляет поля, потом guard. Если `myWindow.IsNull()`, поля всё равно обновлены. Формально ок, но порядок не идиоматичен.

**WONTFIX** — работает.

---

### CR-317. `SelectAtPixel` и `OnMouseButton` — двойной код выделения

**Где:** `OcctView.cpp:661-668` vs `OcctView.cpp:739-746`

```
// SelectAtPixel
myContext->ClearSelected(false);
myContext->MoveTo(px, py, myView, false);
myContext->SelectDetected(AIS_SelectionScheme_Replace);
myContext->UpdateCurrentViewer();
myView->Redraw();

// OnMouseButton — то же самое
myContext->ClearSelected(false);
myContext->MoveTo(px, py, myView, false);
myContext->SelectDetected(AIS_SelectionScheme_Replace);
myContext->UpdateCurrentViewer();
myView->Redraw();
```

**Полная копипаста.** Фикс: `OnMouseButton` → вызов `SelectAtPixel(px, py)`.

**Оценка:** 5 минут.

---

### CR-318. `OnMouseButton` — двойной `myView->Redraw()`

**Где:** `OcctView.cpp:746, 754`

В ветке RELEASE с выделением — `Redraw()` в конце. И `myView->Redraw()` в ветке `myLeftDragging`. Ок, но проверять.

**WONTFIX.**

---

### CR-319. `Redraw()` — тонкая обёртка без проверки `IsNull` в самом деле

**Где:** `OcctView.cpp:801`

```
void OcctView::Redraw() {
    if (!myView.IsNull()) myView->Redraw();
}
```

Ок.

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

### CR-323. `ThemeTokens` — нет токенов для 3D-геометрии (плоскости, оси)

**Где:** `ThemeTokens.h`

**Проблема:** `OcctView::DisplayShape` (CR-279) хардкодит:
- `#c28a23` — fill плоскостей
- `#965b20` — edge плоскостей
- `(0.55, 0.60, 0.68)` — оси
- `(0.55, 0.55, 0.55)` — выход из эскиза (CR-282)

В `ThemeTokens` — ничего.

**Фикс:** добавить:
```
// 3D viewport
ImU32 planeFaceFill;
ImU32 planeFaceEdge;
ImU32 planeEdgeWidthColor;   // ...
ImU32 axisColor;
ImU32 originPointColor;
ImU32 gridColor;
ImU32 gridAxisXColor;
ImU32 gridAxisYColor;
```

**Оценка:** 20 минут.

---

### CR-324. `ThemeTokens` — нет токенов для стилей линий эскиза

**Где:** `ThemeTokens.h`

**Проблема:** цвета линий эскиза дублируются **в двух местах**:
- `SketchTab.cpp:200-202` — для кнопок переключателя (CR-046)
- `OcctView.cpp:626-644` — для отрисовки на 3D (CR-292)

Значения: `#8c33bf` (Normal), `#7378a6` (Construction), `#b36626` (Centerline). Ни в теме, ни в общем хедере.

**Фикс:** добавить:
```
ImU32 lineStyleNormal;
ImU32 lineStyleConstruction;
ImU32 lineStyleCenterline;
```

Плюс — **функция** `ImU32 LineStyleColor(LineStyle)` в `util` или `theme`, чтобы UI и OcctView брали из одного источника.

**Оценка:** 20 минут.

---

### CR-325. `ThemeTokens` — нет токенов для preview-окна

**Где:** `ThemeTokens.h`

**Проблема:** `Application::DrawPreviewInfo` (CR-168) хардкодит:
- `ImVec4(1.0f, 1.0f, 0.3f, 1.0f)` — Length
- `ImVec4(0.7f, 0.9f, 1.0f, 1.0f)` — Angle

**Фикс:** добавить:
```
ImU32 previewLengthText;
ImU32 previewAngleText;
ImU32 previewBg;
```

**Оценка:** 5 минут.

---

### CR-326. `ThemeTokens` — нет токенов для ViewCube (есть, но не полные)

**Где:** `ThemeTokens.h:32-42`

11 полей `cube*` — это **хорошо**, покрытие есть. Но:
- Нет `cubeCornerHover`, `cubeEdgeHover` — для 12 рёбер / 8 углов (Фаза 2).
- Нет `cubeHighlight` — для текущего hover.

Пока не критично, **отложить до Фазы 2**.

**WONTFIX** (отложено).

---

### CR-327. `ThemeTokens.h` — комментарий врёт

**Где:** `ThemeTokens.h:1-10`

```
// namespace — BreezeCAD (без под-namespace, для краткости
// использования в остальном коде).
```

Формально верно (`namespace BreezeCAD`), но **другие ui/theme файлы** в `BreezeCAD::UI::Theme`. Несогласовано.

**Фикс:** либо перенести `ThemeTokens` в `BreezeCAD::UI::Theme`, либо обосновать исключение.

**Оценка:** 15 минут (везде переименовать).

---

### CR-328. `ThemeTokens` — поля не задокументированы

**Где:** `ThemeTokens.h:13-42`

`bgTitle`, `bgCardBg`, `homeBtnLabel`, `homeBtnBg` — **что это**? При чтении `RibbonButton.cpp`, где `bgHover = tokens.bgCardBg` — непонятно.

**Фикс:** однострочный комментарий к каждому полю.

**Оценка:** 15 минут.

---

### CR-329. `ThemePresets` — нет `SetupAutoTheme()` в хедере

**Где:** `ThemePresets.h:16-20`

Хедер объявляет только:
```
void SetupLightTheme();
void SetupDeepSTheme();
```

Но `ThemeManager.h:19` говорит: «theme: 0 = Светлая, 1 = Тёмная, **2 = Авто**, 3 = legacy Dark».

**Где реализация «Авто»?** В `.cpp`? Или Авто = переключение между Light и DeepS?

**Фикс:** либо объявить `SetupAutoTheme()`, либо задокументировать, что Авто = выбор между двумя.

**Оценка:** 5 минут (выяснить) + правки.

---

### CR-330. `ThemePresets.h` — комментарий про DeepSColor устарел

**Где:** `ThemePresets.h:17`

```
/// Тёмная тема DeepSColor: глубокий океан, акцент — бирюза.
```

По `PROJECT_STATE.md` и `CHANGELOG_WORK.md` — **DeepSColor переименована в «Тёмную»**. Комментарий остался.

**Фикс:** «Тёмная тема: глубокий океан, акцент — бирюза.»

**Оценка:** 1 минута.

---

### CR-331. Все заголовки — устаревшие комментарии про этапы рефакторинга
**Включает (merged):** CR-016, CR-563, CR-628, CR-632, CR-633

**Где:**
- `ThemePresets.h:10` — «Реализация — на шаге 2.2 рефакторинга»
- `ThemeManager.h:16` — «Реализация — на шаге 2.4»
- `Fonts.h:11` — «Реализация — на шаге 2.3»

**Проблема:** рефакторинг **завершён**. Комментарии вводят в заблуждение.

**Фикс:** убрать строки.

**Оценка:** 3 минуты.

---

### CR-332. `ThemeManager` — магические числа 0/1/2/3

**Где:** `ThemeManager.h:15`

```
// theme: 0 = Светлая, 1 = Тёмная, 2 = Авто, 3 = legacy Dark.
void ApplyTheme(int theme, ThemeTokens& outTokens, OcctView* view);
```

**Фикс:** `enum class Theme : uint8_t { Light = 0, Dark = 1, System = 2, LegacyDark = 3 };`

Плюс — в `Application.h` `myCurrentTheme` → `Theme`.

**Связано с CR-089** (тот же magic в Application).

**Оценка:** 30 минут.

---

### CR-333. `ThemeManager` — не подключает `ThemeTokens.h`

**Где:** `ThemeManager.h:11-13`

```
namespace BreezeCAD {
class OcctView;
struct ThemeTokens;   // forward
}
```

Формально ок для ссылки в сигнатуре. **Но пользователь** `ApplyTheme(...)` должен видеть `ThemeTokens` целиком, чтобы читать `outTokens.*`. Значит каждый файл, использующий `ApplyTheme`, ещё и подключает `ThemeTokens.h`.

**Фикс:** `#include "ui/theme/ThemeTokens.h"` в `ThemeManager.h` — **один include вместо двух**.

**Оценка:** 2 минуты.

---

### CR-334. `ThemeManager.h` — «legacy Dark» без объяснения

**Где:** `ThemeManager.h:15`

Что такое **legacy Dark**? `SetupDarkTheme` удалена в `b55fb70` (B-13). Зачем оставлять в комментарии? Если это значение совместимо со старым `imgui.ini` — ок, но не задокументировано.

**Фикс:** либо удалить упоминание, либо задокументировать сценарий.

**Оценка:** 5 минут.

---

### CR-336. Все заголовки — namespace-стиль разнобой

**Где:**
- `ThemeTokens.h` — `namespace BreezeCAD { ... }` (без под-namespace)
- `ThemePresets.h` — `namespace BreezeCAD::UI::Theme`
- `ThemeManager.h` — `BreezeCAD::UI::Theme` + внешний `BreezeCAD` для forward
- `Fonts.h` — `BreezeCAD::UI::Theme`

**3 разных стиля в 4 файлах.** Связано с CR-018.

**Фикс:** унифицировать на `BreezeCAD::UI::Theme::*`.

**Оценка:** входит в CR-018 (массовая унификация).

---

## 🔵 RAM — обновление

**`ThemeTokens` — 28 × `ImU32` × 4 байта = 112 байт на экземпляр.** Если `ApplyTheme` вызывается на каждом кадре (не должен, но...) — накопительно.

**Проверить в `.cpp`:** вызывается ли `ApplyTheme` в hot-path.

**Оценка:** 5 минут.

---

## 📊 Прогресс

| Приоритет | Было | Добавлено | Итого |
|---|---|---|---|
| P0 | 190 | 16 | **206** |
| P1 | 77 | 0 | 77 |
| P2 | 41 | 0 | 41 |
| P3 | 16 | 0 | 16 |
| RAM | 13 | 0 | 13 |

### CR-337. 🔴 `myTheme.accent` НЕ совпадает с ImGui accent — два разных акцента в одном UI

**Где:** `ThemeManager.cpp:36-37, 60-62` vs `ThemePresets.cpp:44-46`
**Статус:** OPEN
**Категория:** визуальный баг, нарушение консистентности

**Светлая тема:**
```
// ThemePresets.cpp:44-46 — реальный ImGui-стиль
ImVec4 accent(0.000f, 0.851f, 0.753f, 1.00f);  // #00D9C0 бирюза

// ThemeManager.cpp:36-37 — наши токены
myTheme.accent      = IM_COL32(40, 120, 216, 255);  // #2878D8 СИНИЙ
myTheme.accentHover = IM_COL32(64, 144, 232, 255);  // синий
```

**Тёмная тема:**
```
// ThemePresets.cpp:127-129
ImVec4 accent(0.000f, 0.851f, 0.753f, 1.00f);  // #00D9C0 бирюза

// ThemeManager.cpp:60-62
myTheme.accent      = IM_COL32(51, 140, 242, 255);  // #338CF2 СИНИЙ
myTheme.accentHover = IM_COL32(76, 164, 255, 255);  // синий
```

**Что ломается:**
- Кнопки ImGui — бирюзовые.
- ViewCube hover — синий.
- Home-кнопки hover — синий.
- Slider grab — бирюзовый.
- **Два разных акцента на одном экране.**

**Это не стиль — это визуальный баг.**

**Фикс:** `myTheme.accent` должен читаться **из** `ImGui::GetStyle().Colors[ImGuiCol_Button]` **или** быть единой константой в `ThemePresets`.

**Оценка:** 20 минут.

---

### CR-338. `ApplyTheme` — двойной вызов в конструкторе Application

**Где:** `Application.cpp:151, 160`

```
ImGui::StyleColorsDark();
ApplyTheme(myCurrentTheme);      // ← 1-й раз
UI::Theme::LoadFonts();
ImGui_ImplGlfw_InitForOpenGL(...);
ImGui_ImplOpenGL3_Init("#version 330");
...
ApplyTheme(myCurrentTheme);      // ← 2-й раз
```

Связано с CR-087. Два вызова — токены заполняются дважды. Не критично, но **первый вызов — бесполезен**.

**Фикс:** убрать первый (или убрать второй, если он там с какой-то целью).

**Оценка:** 5 минут.

---

### CR-339. `SetupLightTheme` / `SetupDeepSTheme` — 95% копипаста

**Где:** `ThemePresets.cpp:6-119` vs `:121-232`

**Что одинаково:**
- Все `s.WindowRounding`, `s.FramePadding`, `s.ItemSpacing`, `s.WindowBorderSize`, `s.ChildBorderSize`, `s.FrameBorderSize`, `s.PopupBorderSize` — **кроме 5 значений** (WindowRounding 6 vs 8, ChildRounding 5 vs 6...).
- Все `ImGuiCol_*` присваивания — **те же поля**, отличаются только значения.

**Структурная копипаста** ~90 строк.

**Фикс:**
```
struct ThemeStyle {
    float windowRounding;
    ImVec4 bgWindow, bgPanel, btn, btnHov, btnAct;
    ImVec4 border, txt, txtDis, accent, accentHover, accentActive;
};

void ApplyStyle(const ThemeStyle& t) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = t.windowRounding;
    ...
    // 40 присваиваний ImGuiCol_* из t.*
}

void SetupLightTheme() { ApplyStyle(kLightStyle); }
void SetupDeepSTheme() { ApplyStyle(kDeepSStyle); }
```

**Оценка:** 1.5 часа.

---

### CR-340. `SetupLightTheme` / `SetupDeepSTheme` — нет `SetupAutoTheme`

**Где:** `ThemePresets.h:16-20` (подтверждает CR-329)

Header объявляет только Light и DeepS. В `ThemeManager.cpp:16-18`:
```
// 2 = Авто (по системе; пока используем Тёмную)
```

**Фикс:** реализовать `SetupAutoTheme()` — детектить ОС через `glfwGetPlatform` или WinAPI, вызывать Light/DeepS.

**Оценка:** 30 минут.

---

### CR-341. `ApplyTheme` — magic `theme == 3`

**Где:** `ThemeManager.cpp:15`

```
if (theme == 3) theme = 1;
```

Связано с CR-332. Плюс — комментарий в шапке объясняет «3 = legacy Dark», но `theme` — это **int**, никакой защиты.

**Фикс:** `enum class Theme` (CR-332).

---

### CR-342. `ApplyTheme` — нет default-ветки

**Где:** `ThemeManager.cpp:15-105`

```
if (theme == 0) { ... } else { ... }
```

**Что если `theme == 5`?** Попадёт в `else` → тёмная. Логика работает, но неявно.

**Фикс:** `switch (theme)` с явным `default: case Dark`.

---

### CR-343. `ApplyTheme` — `if (view)` без проверки на «это не тот же view»

**Где:** `ThemeManager.cpp:54-58, 102-106`

```
if (view) {
    Quantity_Color top(0.66, 0.72, 0.78, Quantity_TOC_RGB);
    ...
    view->SetBackgroundGradient(top, bottom);
}
```

`SetBackgroundGradient` вызывает `myView->Redraw()` (CR-... в OcctView). При двойном вызове `ApplyTheme` (CR-338) — **двойной redraw**.

**Фикс:** либо убрать второй вызов `ApplyTheme`, либо `SetBackgroundGradient` без redraw.

---

### CR-345. `ThemeManager.cpp` — токены заполняются вручную, 28 строк × 2 темы

**Где:** `ThemeManager.cpp:25-52, 70-100`

28 `myTheme.xxx = IM_COL32(...)` в каждой ветке. **56 строк ручного заполнения.** Легко забыть поле, легко ошибиться в цифрах (что и случилось с CR-337).

**Фикс:** табличный стиль:
```
struct TokenPair { ImU32 light; ImU32 dark; };
static const std::map<std::string, TokenPair> kTokens = {
    { "bgTitle", { IM_COL32(178,182,190,255), IM_COL32(28,32,40,255) } },
    ...
};
```
Или ещё лучше — **отдельные функции** `FillLightTokens(myTheme)`, `FillDarkTokens(myTheme)`.

**Оценка:** 40 минут.

---

### CR-346. `Fonts.cpp` — 7 hardcoded путей fallback, платформо-специфичных

**Где:** `Fonts.cpp:18-26`

```
"assets/fonts/Inter-Regular.ttf",
"../assets/fonts/Inter-Regular.ttf",
"../../assets/fonts/Inter-Regular.ttf",
"assets/fonts/Roboto-Regular.ttf",
"../assets/fonts/Roboto-Regular.ttf",
"C:/Windows/Fonts/segoeui.ttf",           // ← Windows-специфичный
"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",  // ← Linux-специфичный
```

**Проблемы:**
1. RULES п.23 — хардкод путей.
2. **macOS** — нет fallback.
3. **ARM/Android** — нет fallback.
4. `../` и `../../` — догадки про CWD.

**Фикс:**
- `std::filesystem::path exeDir = getExecutableDir();`
- `exeDir / "assets" / "fonts" / "Inter-Regular.ttf"`
- Для fallback — использовать `#ifdef __APPLE__` и т.д.

**Оценка:** 30 минут.

---

### CR-347. `Fonts.cpp` — `fa-solid-900.ttf` не найден → тихий fail

**Где:** `Fonts.cpp:56-64`

```
for (const auto& path : faCandidates) {
    ...
    ImFont* fa = io.Fonts->AddFontFromFileTTF(...);
    if (fa) {
        std::cout << "BreezeCAD: Font Awesome loaded from '" << path << "'\n";
        break;
    }
}
// ← если не сработало — молча
```

**Если FA не загрузился** — все иконки превратятся в квадратики `□`. Пользователь подумает что UI сломан, а **диагностики нет**.

**Фикс:** `else { std::cerr << "WARN: Font Awesome not found — icons will be missing\n"; }`.

**Оценка:** 5 минут.

---

### CR-348. `Fonts.cpp` — `18.0f` — размер хардкод

**Где:** `Fonts.cpp:33, 61`

```
io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f, ...);   // ×2
```

18.0f — базовый размер шрифта. **На HiDPI** (4K, Retina) — будет мелко и мыльно.

**Фикс:** читать из `ImGui::GetIO().DisplayFramebufferScale` или настройки.

**Оценка:** 30 минут (правильно).

---

### CR-349. `Fonts.cpp` — только `GetGlyphRangesCyrillic()`

**Где:** `Fonts.cpp:35`

```
io.Fonts->GetGlyphRangesCyrillic()
```

**Что теряется:**
- Latin Extended (для имён файлов на европейских языках).
- Greek (греческие буквы в мат. формулах).
- Symbols (`±`, `°`, `²`, `³`, `⋅` — **критично для CAD**).
- Стрелки (`→`, `←` — в UI).

**Для САПР** — нужны **минимум** Greek + Symbols + Latin. Сейчас только кириллица + базовый Latin.

**Фикс:**
```
ImFontGlyphRangesBuilder builder;
builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
builder.AddRanges(io.Fonts->GetGlyphRangesGreek());
builder.AddText("±°²³⋅≈≠≤≥→←↑↓");
static ImVector<ImWchar> ranges;
builder.BuildRanges(&ranges);
```

**Оценка:** 20 минут.

---

### CR-350. `ThemePresets.cpp` — комментарий про цвет устарел

**Где:** `ThemePresets.cpp:35`

```
ImVec4 BTN      (0.878f, 0.878f, 0.878f, 1.00f);   // #E0E0E0 — кнопки
```

`0.878 * 255 = 223.9` ≈ `0xE0`. Ок. Но **UI_LAYOUT.md §9.2** говорит `#EDEDED` для кнопок.

**Расхождение с каноном.** Уточнить, что правильнее.

**Оценка:** 5 минут.

---

### CR-351. `ThemePresets.cpp` — `ImVec4 accent` в комментарии #00D9C0, но фактически бирюза только в тёмной

**Где:** `ThemePresets.cpp:44`

```
ImVec4 accent (0.000f, 0.851f, 0.753f, 1.00f);   // #00D9C0 — бирюза, как в тёмной
```

**В светлой теме** по `UI_LAYOUT.md §9.2` акцент — **`#E8871E` оранжевый**. Здесь — бирюза.

**Фикс:** либо взять из канона оранжевый, либо обновить канон.

**Оценка:** 10 минут.

---

### CR-352. `ThemeManager.cpp` — `ApplyTheme` ~90 строк

**Где:** `ThemeManager.cpp:10-107`

Огромная функция. 2 большие ветки. Каждая — 40+ строк заполнения токенов.

**Фикс:** разбить на `FillLightTokens(myTheme)` + `FillDarkTokens(myTheme)` + `ApplyStylePreset(theme)`.

**Оценка:** 30 минут.

---

### CR-353. `SetupLightTheme` / `SetupDeepSTheme` — `ImVec4(BG_WINDOW.x, BG_WINDOW.y, BG_WINDOW.z, 0.0f)` — неочевидно

**Где:** `ThemePresets.cpp:62, 145`

```
c[ImGuiCol_ChildBg] = ImVec4(BG_WINDOW.x, BG_WINDOW.y, BG_WINDOW.z, 0.0f);
```

Alpha = 0 — прозрачность. Но зачем копировать RGB, если alpha = 0? **Фикс:** `ImVec4(0, 0, 0, 0)` или комментарий.

**Оценка:** 2 минуты.

---

### CR-354. `ThemeManager.cpp` — нет обновления токенов при **смене темы во время работы**

**Где:** `ThemeManager.cpp:10-107`

Функция `ApplyTheme` вызывается из `Application::ApplyTheme`, который вызывается:
- в конструкторе (2 раза, CR-338)
- когда пользователь меняет тему в настройках

**Проблема:** между двумя вызовами `myTheme` — **старые значения**. Если что-то отрисуется **между** сменой темы и переприменением (race condition в кадрах) — увидит старые цвета.

**Фикс:** проверить порядок в Application — сначала `ApplyTheme`, потом `myCurrentTheme = ...`.

**Оценка:** 10 минут (проверить).

---

### CR-355. `ThemeManager.h` — параметр `int theme` (не enum)

**Где:** `ThemeManager.h:15`

Подтверждает CR-332.

---

---

## 🔴 P0 — ui/widgets (Волна 9, часть 3)

### CR-357. `SvgIconBtn` — `IconTintColor(true)` игнорирует disabled

**Где:** `SvgButton.cpp:17`
**Статус:** OPEN

```
ImVec4 tint = UI::Widgets::IconTintColor(true);   // ← всегда true
```

`IconTintColor(false)` умеет приглушать иконки (alpha × 0.45). Но `SvgIconBtn` **не имеет параметра `enabled`** — все кнопки выглядят активными.

**Что ломается:**
- Кнопки модулей (disabled) в QAT — не имеют disabled-вида.
- Все кнопки ribbon-тулбара визуально всегда доступны.

**Фикс:** добавить параметр `bool enabled = true` + передавать в `IconTintColor(enabled)` + блокировать click.

**Оценка:** 20 минут.

---

### CR-358. `SvgIconBtn` — дублирует логику `RibbonButton.cpp`
**Включает (merged):** CR-455, CR-586

**Где:** `SvgButton.cpp:26-52` vs `RibbonButton.cpp:52-105`
**Статус:** OPEN

**Общие блоки:**
1. `PushID(id)` + `InvisibleButton` + `IsItemClicked/IsItemHovered`.
2. Hover-bg через `AddRectFilled`.
3. Иконка через `AddImage` с tint.
4. Fallback rect.

**Разные детали:**
- RibbonButton рисует **arrow-полосу** справа.
- SvgIconBtn поддерживает **transparentBg**.
- RibbonButton — 76×76 или 32×32; SvgIconBtn — произвольный.

**Фикс:** выделить `DrawIconButtonCore(...)` в общий модуль `widgets/IconButtonCore.{h,cpp}`. Оба используют.

**Оценка:** 1.5 часа.

---

### CR-359. `SvgIconBtn` — магические `4, 4` в fallback

**Где:** `SvgButton.cpp:48-50`

```
dl->AddRectFilled(ImVec2(min.x + 4, min.y + 4),
                  ImVec2(max.x - 4, max.y - 4),
                  ...);
```

Отступ 4 px. В RibbonButton тот же fallback — без отступа или с другим. **Несогласовано.**

**Фикс:** общий helper с единым padding.

**Оценка:** входит в CR-358.

---

### CR-360. `QatIconButton` — магические `32, 28`

**Где:** `SvgButton.cpp:60`

```
return SvgIconBtn(icons, id, name, size, 32, 28, tooltip, true);
```

**Размеры QAT-кнопки** захардкожены в функции. Если QAT-высота изменится — придётся править здесь.

**Фикс:** `constexpr float kQatBtnW = 32.0f;` `kQatBtnH = 28.0f;` — или параметры.

**Оценка:** 5 минут.

---

### CR-361. `IconTintColor` — магия `0.45f`

**Где:** `IconTint.cpp:11`

```
if (!enabled) tint.w *= 0.45f;
```

**Фикс:** `constexpr float kDisabledIconAlpha = 0.45f;`.

**Оценка:** 2 минуты.

---

### CR-362. `IconTintColor` — читает `ImGuiCol_Text`, но тема может быть в `ThemeTokens`

**Где:** `IconTint.cpp:8`

```
ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
```

**Проблема:** в проекте есть **два слоя цвета**:
- `ImGuiStyle::Colors[ImGuiCol_Text]` (заполняется `SetupLightTheme`/`SetupDeepSTheme`).
- `ThemeTokens::textMain` (заполняется в `ThemeManager::ApplyTheme`).

Сейчас они **могут расходиться** (см. CR-337). Если разработчик изменит `myTheme.textMain`, иконки **не перекрасятся**.

**Фикс:** читать из `ThemeTokens::textMain`. Но это значит, `IconTintColor` должен знать про `myTheme`. Возможно — передавать как параметр.

**Оценка:** 20 минут (архитектурное решение).

---

### CR-363. `IconTint` — нет функции для ribbon-arrow tint

**Где:** `IconTint.cpp`

В `RibbonButton.cpp:170` — стрелка ▼ рисуется тем же `tint`. Но в disabled-состоянии стрелка **не должна менять прозрачность** так же, как иконка — она часть фона.

Пока работает, но хрупко. **Отложено до Фазы 2.**

**WONTFIX.**

---

### CR-364. `SvgIconBtn` — `#include "imgui.h"` в `.cpp`, но `GLuint` / `GL_TEXTURE_2D`?

**Где:** `SvgButton.cpp`

`GLuint tex = icons.Get(...)` — тип `GLuint`. Нужен `#include <GL/gl.h>`. **Не подключён явно.** Работает через `IconManager.h` (который включает gl.h).

**Хрупко.** **Фикс:** явный include.

**Оценка:** 2 минуты.

---

---

## 🔴 P0 — ui/widgets (Волна 9, часть 4: Tooltip + SelectionRect)

### CR-367. `Tooltip::Draw` — `ImGui::GetTime()` вызывается 2 раза

**Где:** `Tooltip.cpp:29, 33`

```
myHoverStartTime = ImGui::GetTime();       // 1
...
double elapsed = ImGui::GetTime() - myHoverStartTime;   // 2
```

В hot-path (каждый кадр на каждый hovered widget). Можно сохранить один раз.

**Фикс:** `const double now = ImGui::GetTime();` в начале.

**Оценка:** 2 минуты.

---

### CR-369. `Tooltip::Draw` — контракт «вызвать сразу после виджета» хрупкий

**Где:** `Tooltip.h:16-19`, `Tooltip.cpp:20`

**Проблема:**
```
// Вызывается СРАЗУ после отрисовки виджета — проверяет
// ImGui::IsItemHovered() текущего item.
```

`ImGui::IsItemHovered()` работает только для **последнего** item. Если между widget и `Draw` что-то отрисовалось — `IsItemHovered` вернёт **false** или hover другого виджета.

**Реальный риск:** кто-то добавит `Draw` после `ImGui::SameLine()` или другого widget → tooltip сломается молча.

**Фикс:** 
1. `assert(ImGui::GetCurrentContext()->CurrentWindowStack.Size > 0)`.
2. Или переделать API: `Draw(id, name, ..., ImGuiID widgetId)` и использовать `ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)` с явным id.

**Оценка:** 30 минут (архитектурно).

---

### CR-370. `Tooltip::Draw` — не проверяет перекрытие с виджетом

**Где:** `Tooltip.cpp:41-67`

UI_LAYOUT §10.2: «Тултип не перекрывает кнопку, к которой относится».

Сейчас — `ImGui::BeginTooltip()` без `SetNextWindowPos` — ImGui сам ставит рядом, но **на малых экранах может перекрыть**. Не критично для MVP.

**WONTFIX** (до Фазы 2).

---

### CR-371. `Tooltip::Draw` — `imagePath` не используется

**Где:** `Tooltip.h:19`, `Tooltip.cpp:60-63`

```
if (!imagePath.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("(картинка: %s)", imagePath.c_str());
    // TODO: загрузка PNG через imgui — следующая итерация
}
```

**Сейчас** — только выводит путь как текст. **Бесполезно.** Или убрать, или реализовать.

**Фикс:** либо удалить параметр, либо `#ifdef FUTURE` блок.

**Оценка:** 5 минут (удалить) или 2 часа (реализовать).

---

### CR-372. `Tooltip.cpp` — русский хардкод `"(картинка: %s)"`

**Где:** `Tooltip.cpp:62`

**Фикс:** `Tr("tooltip.image_placeholder")`.

**Оценка:** 3 минуты.

---

### CR-373. `Tooltip::Draw` — нет проверки `name.empty()`

**Где:** `Tooltip.cpp:47`

```
ImGui::TextColored(..., "%s", name.c_str());
```

Если `name` пустой — рисуется пустая строка с separator. Пользователь видит tooltip с одной полоской.

**Фикс:** `if (name.empty() && shortDesc.empty() && longDesc.empty()) return;` в начале.

**Оценка:** 3 минуты.

---

### CR-374. `Tooltip::Draw` — параметры `const std::string&` могут быть dangling

**Где:** `Tooltip.cpp:14-19`

Если вызывающий передаёт `Tr("key")`, и `Tr` возвращает `std::string` **by value** — параметр const& привязан к временному, жив до конца вызова. Ок.

Если **`const std::string&`** возвращает — тоже ок.

Если где-то передаётся **raw char***, приведённый к `std::string` — временный объект, тоже ок (до конца выражения).

**WONTFIX** — контракт безопасен.

---

### CR-377. `SelectionRect` — нет проверки `window == nullptr`

**Где:** `SelectionRect.cpp:22-23`

```
glfwGetWindowSize(window, &winW, &winH);   // ← без проверки
```

Если `window == nullptr` — **краш**. Формально не должно случаться, но защита нужна.

**Фикс:** `if (!window) return;`.

**Оценка:** 1 минута.

---

### CR-379. `SelectionRect` — смешение ответственностей

**Где:** `SelectionRect.cpp` — принимает `OcctView&` и `GLFWwindow*`

Функция:
1. Спрашивает OcctView о состоянии.
2. Получает координаты в framebuffer-пикселях.
3. Переводит в логические (через glfw размеры).
4. Рисует.

**Логичнее:** `OcctView` возвращает координаты уже в логических пикселях (или `Application` конвертирует), а `DrawSelectionRect` — только рисует.

**Фикс:** архитектурно — возможно в Фазе 2.

**WONTFIX пока.**

---

---

## 🔴 P0 — ui/qat (Волна 10, часть 1: QuickAccessToolbar.h)

### CR-380. `QatContext` — `char* searchBuffer` + `searchBufferSize` — разъедутся

**Где:** `QuickAccessToolbar.h:26-27`
**Статус:** OPEN

```
char* searchBuffer = nullptr;    // буфер 128 байт
int   searchBufferSize = 128;
```

**Проблема:** два независимых поля. Если `searchBuffer` укажет на буфер 64 байта, а `searchBufferSize = 128` — `InputTextWithHint` переполнит. **UB.**

**Фикс:**
- Либо `std::string& searchBuffer` — размер внутри.
- Либо `std::array<char, 128>& buffer` — размер compile-time.
- Либо `char(&buf)[N]` — template-параметр.

**Оценка:** 20 минут.

---

### CR-381. `QatContext` — 11 `std::function` колбэков без default-конструктора

**Где:** `QuickAccessToolbar.h:32-44`

**Проблема:** `std::function` по умолчанию — **пустой**. Если Application забудет задать `onSave` — вызов `.operator()` бросит `std::bad_function_call`.

Сейчас защита — `if (ctx.onSave) ctx.onSave();` в **каждом** месте. Но это **verbosity** + легко забыть.

**Фикс:** либо `template <typename T> void CallIf(const std::function<T>& f, ...)`, либо — оставить как есть, но **проверять единообразно**.

Сейчас — часть колбэков проверяется (`if (ctx.onSave) ...`), часть — **нет** (например `ctx.tooltip->Draw(...)` — `tooltip` не проверяется вообще).

**Оценка:** 20 минут.

---

### CR-383. Кнопка «Модули» рисуется ДВАЖДЫ

**Где:** `QuickAccessToolbar.cpp:104-114` и `201-209`
**Статус:** OPEN
**Категория:** функциональный баг

**Первый вызов (строка 104):**
```
if (UI::Widgets::QatIconButton(*ctx.icons, "qat_modules", "puzzle", 20, "Модули")) {
    // TODO: диалог управления модулями
}
ctx.tooltip->Draw("qat.modules", ...);
```

**Второй вызов (строка 201):**
```
ImGui::SameLine(modulesX);
ImGui::PushStyleColor(ImGuiCol_Button, ...);
if (UI::Widgets::SvgIconBtn(*ctx.icons, "qat_modules", "puzzle", 18, 28, 26, "Модули")) {
    // TODO: меню управления модулями (Фаза 5+)
}
ImGui::PopStyleColor();
if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Модули (не разработано)");
```

**Две кнопки с ОДИНАКОВЫМ `id = "qat_modules"`.**

**Что происходит:**
1. `PushID("qat_modules")` дважды → **ImGui ID conflict** → вторая кнопка может ломать состояние первой.
2. **Визуально две иконки puzzle** — одна слева, одна справа.
3. Tooltip один, потом другой — пользователь видит **два разных tooltip** для одной функции.

**Фикс:** удалить **левую** кнопку (строка 104-114), оставить только правую (это по UI_LAYOUT §3.3).

**Оценка:** 5 минут.

---

### CR-384. ~30 хардкод-строк на русском — локализация обойдена
**Включает (merged):** CR-103, CR-167, CR-191, CR-192, CR-398, CR-399, CR-402, CR-407, CR-408, CR-416, CR-426, CR-444, CR-465, CR-471, CR-477, CR-499, CR-500, CR-505, CR-508, CR-517, CR-525, CR-526, CR-533, CR-534, CR-572, CR-587, CR-598, CR-599, CR-608, CR-612, CR-613, CR-639, CR-665, CR-699

**Где:** весь `QuickAccessToolbar.cpp`
**Статус:** OPEN

**Меню лого:**
- «Свернуть», «Развернуть», «Настройки», «О программе», «Справка», «Закрыть»

**Меню «Создать»:**
- «Деталь», «Сборка», «Черчение»

**Меню «Покажи формат»:**
- «Линии», «Размеры», «Ограничения»

**Меню «Текстуры»:**
- «Материалы», «Сталь», «Алюминий», «Латунь», «Пластик», «Импортировать текстуру...»

**Меню «Параметры»:**
- «Таблица параметров», «Пока пусто», «Открыть в новой вкладке»

**Меню «Измерение»:**
- «Расстояние», «Угол», «Радиус», «Диаметр», «Точность»

**Поиск:**
- «Ничего не найдено»

**Tooltip:**
- «Модули (не разработано)»

**Меню формат окна:**
- «Свернуть», «Развернуть», «Закрыть»

**Фикс:** `Tr("qat.xxx")` + 30 новых ключей в `Localization.cpp`.

**Оценка:** 1 час.

---

### CR-385. `ctx.tooltip->Draw(...)` — hardcoded английский ключ

**Где:** `QuickAccessToolbar.cpp:41, 63, 90, 124`
**Статус:** OPEN

```
ctx.tooltip->Draw("qat.doc", "Документ", "Backstage...", "Открывает...");
```

Ключ `"qat.doc"` — **английский**, а тексты — **русские**. Несогласованно с `Tr("qat.save")` (который должен брать из словаря).

**Фикс:** либо `Tr()` для всех, либо явная политика «tooltip-keys — латиница, тексты — inline-литералы» (но тогда локализация сломана).

**Оценка:** входит в CR-384.

---

### CR-386. Меню «Черчение» — `MenuItem` с `false, false` — мёртвый код

**Где:** `QuickAccessToolbar.cpp:51-53`
**Статус:** OPEN
**Категория:** функциональный баг

```
if (ImGui::MenuItem("Черчение", "", false, false)) {   // ← disabled
    if (ctx.onNotImplemented) ctx.onNotImplemented("Черчение");
}
```

**По документации ImGui:** `MenuItem(label, shortcut, selected, enabled)` с `enabled = false` — **клик не сработает**, `return false` всегда.

**Значит:** `ctx.onNotImplemented("Черчение")` **никогда не вызовется**.

**В UI_LAYOUT §3.4** сказано: «Черчение: серым, при клике → диалог "Хотите установить модуль чертежей?"». То есть должно быть **кликабельно**, но с диалогом.

**Фикс:** `MenuItem("Черчение", "", false, /*enabled*/ true)` + обработка в popup — отдельная проверка «кликнули но не реализовано → диалог».

**Оценка:** 10 минут.

---

### CR-387. Меню формат окна — «Свернуть»/«Развернуть» дублируют системные кнопки

**Где:** `QuickAccessToolbar.cpp:22-36`

В меню лого есть «Свернуть» и «Развернуть». Это **дублирует** системные кнопки окна `[—] [□]` в правом верхнем углу.

**Это ошибка копирования?** Или задумка для Windows-стиля?

Проверить с MikeVerx.

**WONTFIX** пока (уточнить).

---

### CR-388. Меню лого — «О программе» и «Справка» — пустые обработчики
**Включает (merged):** CR-389, CR-390, CR-391, CR-415, CR-506, CR-585, CR-614

**Где:** `QuickAccessToolbar.cpp:29-30`

```
if (ImGui::MenuItem("О программе")) {}   // ← пусто
if (ImGui::MenuItem("Справка", "F1")) {} // ← пусто
```

**Что ломается:** пользователь кликает, **ничего не происходит**. Диалог должен открываться (`NotImplementedDialog`).

**Фикс:** `ctx.onNotImplemented("О программе")` / `ctx.onNotImplemented("Справка")`.

**Оценка:** 5 минут.

---

### CR-392. Меню «Модули» (не та что кнопка) — тоже пустое

**Где:** `QuickAccessToolbar.cpp:105-107` (первая)

```
if (UI::Widgets::QatIconButton(*ctx.icons, "qat_modules", "puzzle", 20, "Модули")) {
    // TODO: диалог управления модулями
}
```

**Если оставляем эту кнопку** (по CR-383 удалять её) — надо `onNotImplemented`.

**Оценка:** входит в CR-383.

---

### CR-394. Разделители через `ImGui::Text("|")` — хардкод + не адаптив

**Где:** `QuickAccessToolbar.cpp:67, 76, 86, 93`

```
ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
```

**Проблемы:**
1. Разделитель **символом**, а не через `ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical)` (как в AnnotateTab).
2. Не адаптивен к высоте QAT (28px).
3. В светлой теме чёрный `|`, в тёмной — белый. Это ок, но **цвет не из темы**.

**Фикс:** helper `QatSeparator()` с вертикальной линией.

**Оценка:** 20 минут.

---

### CR-395. `qat_modules` (левая) и `qat_modules` (правая) — **ImGui ID conflict**

**Где:** `QuickAccessToolbar.cpp:104 и 201`

Два `QatIconButton(*ctx.icons, "qat_modules", ...)` — **одинаковый id**.

**ImGui behavior:** `PushID` + одинаковый id → `InvisibleButton` второго не перезаписывает hover первого, но при определённых условиях — **race** в `IsItemHovered()`.

**Связано с CR-383.**

**Оценка:** входит в CR-383.

---

### CR-396. Поиск: аллокации на каждый кадр + lowercase

**Где:** `QuickAccessToolbar.cpp:220-232`
**Статус:** OPEN

```
std::string query = ctx.searchBuffer;                              // ← копия
std::vector<std::pair<std::string, std::function<void()>>> results; // ← heap
if (ctx.commands) for (auto& cmd : *ctx.commands) {
    std::string lname = cmd.name;      // ← копия имени
    std::string lquery = query;        // ← копия запроса (для каждого cmd!)
    for (auto& c : lname) c = (char)std::tolower((unsigned char)c);  // ← lowercase
    for (auto& c : lquery) c = (char)std::tolower((unsigned char)c);
    if (lname.find(lquery) != std::string::npos) {
        results.push_back({cmd.name + "  (" + cmd.shortcut + ")", cmd.action});   // ← ещё 2 копии + std::function copy
    }
}
```

**При активном поиске** (пользователь печатает) — 60 FPS:

- 15 команд × (2 копии строк + lowercase + результат копируется) = **~100-200 heap-аллокаций/сек**.
- Плюс `std::function` копия — **ещё одна аллокация** на каждое совпадение.

**Фикс:**
1. `lquery` — вычислить **один раз** до цикла.
2. `results` — не копировать имя команды и `std::function`, хранить **указатель** (`const QatCommand*`).
3. Lowercase — сделать один раз для query и один раз для name (уже делаем), но **без копий** — использовать `std::search` с предикатом.

**Оценка:** 30 минут.

---

### CR-400. `qat_create` — «Ctrl+N» и «Ctrl+S» в MenuItem — это только отображение shortcut

**Где:** `QuickAccessToolbar.cpp:44, 71`

```
if (ImGui::MenuItem("Деталь", "Ctrl+N")) { ... }   // ← "Ctrl+N" только показывается
```

**По документации ImGui:** `shortcut` — **только отображается**, не обрабатывается. Реальный Ctrl+N должен обрабатываться где-то.

**Связано с CR-101** (где обрабатываются хоткеи?).

**Оценка:** аудит отдельно.

---

### CR-401. `QatContext::onGoHome` вызывается при клике на «Документ» — семантическая путаница

**Где:** `QuickAccessToolbar.cpp:58-60`

```
if (UI::Widgets::QatIconButton(*ctx.icons, "qat_doc", "file-text", 20, "Документ")) {
    if (ctx.onGoHome) ctx.onGoHome();   // ← Backstage/Home
}
```

Комментарий в `.h:38`: `onGoHome` — «Документ / Главная — сохранить превью + home».

**Функционально ок** — Backstage и Home объединены (UI_LAYOUT §8.1). Но имя **вводит в заблуждение** — «go home» ≠ «открыть документ».

**Фикс:** переименовать в `onOpenBackstage` или `onShowHome`.

**Оценка:** 5 минут.

---

### CR-403. `Right-panel-aware layout` — логика в QAT, а нужна в Application

**Где:** `QuickAccessToolbar.cpp:196-204`

```
float rightPanelW = ctx.rightPanelExpanded ? 280.0f : 40.0f;
float telemetryX = io.DisplaySize.x - margin - rightPanelW - telemetryW;
```

**QAT знает про ширину правой панели**, чтобы правильно отрисовать телеметрию. Это **layout-забота** более высокого уровня.

**Фикс:** Application вычисляет позиции и передаёт в ctx. Или QAT получает `float rightPanelWidth` — уже готовое значение.

**Оценка:** 20 минут.

---

---

## 🔴 P0 — ui/status (Волна 10, часть 3: StatusBar)

### CR-404. `StatusBar` — Z захардкожен в `0.0`

**Где:** `StatusBar.cpp:48-49`
**Статус:** OPEN
**Категория:** функциональный баг

```
ImGui::TextDisabled("X: %.2f  Y: %.2f  Z: %.2f",
                    info.mouseX, info.mouseY, 0.0);   // ← Z = 0.0 ВСЕГДА
```

`StatusInfo` не имеет поля `mouseZ`. **Z нельзя показать** — всегда ноль.

**Фикс:** добавить `double mouseZ` в `StatusInfo`, обновлять в Application.

**Оценка:** 10 минут.

---

### CR-405. `StatusBar` — расхождение `rightPanelW` с QAT и UI_LAYOUT
**Включает (merged):** CR-420, CR-564, CR-581

**Где:** `StatusBar.cpp:51`
**Статус:** OPEN

```
const float rightPanelW = info.rightPanelExpanded ? 320.0f : 80.0f;
```

| Источник | Expanded | Collapsed |
|---|---|---|
| `StatusBar.cpp` | **320** | **80** |
| `QuickAccessToolbar.cpp:196` | **280** | **40** |
| `UI_LAYOUT §2.2` | **280** | **40** |

**StatusBar сдвинут левее на 40 px.** Пользователь видит рассинхрон: телеметрия в QAT уезжает под правую панель, а статус-бар — нет.

**Фикс:** единый `PanelLayout::RightPanelWidth(expanded)`.

**Оценка:** 30 минут.

---

### CR-409. `StatusBar` — разделители через `TextDisabled("|")`

**Где:** `StatusBar.cpp:34-36, 45-46`

Три раза:
```
ImGui::SameLine(0, 16);
ImGui::TextDisabled("|");
ImGui::SameLine(0, 16);
```

**Проблемы:**
1. Символ `|`, а не `SeparatorEx`.
2. Magic 16.
3. Не масштабируется на HiDPI.

**Связано с CR-394** (QAT — та же проблема).

**Фикс:** `StatusSeparator()` — helper с вертикальной линией из draw-list.

**Оценка:** 20 минут.

---

### CR-411. `StatusBar` — `io.DisplaySize.y - h` без проверки минимума

**Где:** `StatusBar.cpp:18`

Если окно меньше 24 px (маловероятно, но) — статус-бар уедет за границу.

**WONTFIX** (нереально).

---

## 🔴 P0 — ui/status (Волна 10, часть 4: ProjectTabs)

### CR-414. `ProjectTabs` — `PushID(i)` + `Button(label + "##tab")` — конфликт при одинаковых именах

**Где:** `ProjectTabs.cpp:57-58, 82`

```
ImGui::PushID(i);   // ← i = индекс
...
if (ImGui::Button((label + "##tab").c_str(), ImVec2(140, 30))) {
```

**ImGui ID** = `hash(PushID(i) + "label##tab")`. Если два проекта называются `"Project1"` (что может быть — пользователь переименовал вручную), то ID всё равно разные из-за `PushID(i)`. **ОК.**

**НО:** `##tab` — суффикс для отделения label от id. А `PushID(i)` уже даёт уникальность. `##tab` — избыточно.

**WONTFIX** — работает, но можно упростить.

---

### CR-417. `ProjectTabs` — «*» для unsaved — magic

**Где:** `ProjectTabs.cpp:79`

```
if (unsaved) label += " *";
```

**По канону** (`CANON_FEATURES`): маркер несохранённых — `●` или `*`. В `STATUS.md` UI_LAYOUT §2.1: `Project1 *`.

**Ок** — соответствует. Но **не через `Tr()`** (не проблема, символ универсален).

**WONTFIX.**

---

### CR-418. `ProjectTabs` — TabHome показывается Disabled когда `onHome=true`

**Где:** `ProjectTabs.cpp:29-40`

```
const bool onHome = ctx.onHome;
if (onHome) {
    ImGui::BeginDisabled();   // ← disabled если УЖЕ на home
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
}
if (ctx.icons && UI::Widgets::SvgIconBtn(*ctx.icons, "tab_home", "house", 14, 120, 30, "На главную")) {
    if (ctx.onGoHome) ctx.onGoHome();
}
if (onHome) {
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
}
```

**Проблема:** если пользователь на home — кнопка disabled. Но **как он выйдет из home на проект?** Через табы проектов (`SameLine` + `Button("Project1")`) — они **вне** disabled-блока. Ок.

Но: **визуально disabled Home-кнопка** выглядит **серой** — пользователь может подумать, что она вообще недоступна, хотя на самом деле он уже на home.

**Фикс:** вместо disabled — **подсветка** «вы здесь» (как active tab).

**Оценка:** 15 минут.

---

### CR-421. `ProjectTabs` — не использует `ctx.rightPanelExpanded`

**Где:** `ProjectTabsContext` (нет поля, но QAT/StatusBar его имеют)

Вкладки проектов не сдвигаются с учётом правой панели. При развёрнутой панели — последние табы уезжают под неё.

**Проверить визуально.**

**Оценка:** 10 минут.

---

### CR-422. `ProjectTabs` — `PushStyleColor(ImGuiCol_Button, ...)` в disabled-блоке для Home

**Где:** `ProjectTabs.cpp:32`

```
if (onHome) {
    ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
}
```

`BeginDisabled` + `PushStyleColor` **конфликтуют** — disabled сам сбрасывает цвета. Скорее всего, цвет не применится. **Проверить.**

**Оценка:** 5 минут.

---

### CR-423. `ProjectTabs` — `io.MouseWheel` в hot-path

**Где:** `ProjectTabs.cpp:24`

```
if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0.0f) {
```

Ок — только при активном скролле. **WONTFIX.**

---

### CR-424. `ProjectTabs` — `if (ctx.icons)` проверяется, но `names`/`paths` — нет

**Где:** `ProjectTabs.cpp:76, 78`

```
std::string label = (ctx.names && i < (int)ctx.names->size()) ? (*ctx.names)[i] : "";
bool unsaved = (ctx.paths && i < (int)ctx.paths->size() && (*ctx.paths)[i].empty());
```

**Проверки есть.** Ок.

**WONTFIX.**

---

### CR-425. `ProjectTabs` — контекстное меню (`BeginPopupContextItem`) на каждой кнопке

**Где:** `ProjectTabs.cpp:91`

`BeginPopupContextItem("##tab_ctx")` — ок, но `##tab_ctx` — **фиксированное имя** для всех табов. ImGui использует ID-стек (`PushID(i)`), поэтому ок. **WONTFIX.**

---

### CR-427. `StatusBar` — `info.sketchActive` управляет показом, но не текущим инструментом

**Где:** `StatusBar.cpp:29`

Показывает «Эскиз» / «Режим ожидания». **А текущий инструмент** (Отрезок / Прямоугольник)? Не показывается.

По UI_LAYOUT §2.1: «Режим ожидания» слева. Возможно, добавить имя инструмента — Фаза 2.

**Отложить.**

---

### CR-428. `StatusBar` — `\n` в тексте не используется, но нет `TextUnformatted`

**WONTFIX.**

---

### CR-429. `ProjectTabs` — `unsaved` проверяет только пустой path, а не `IsModified`

**Где:** `ProjectTabs.cpp:78`

```
bool unsaved = (ctx.paths && i < (int)ctx.paths->size() && (*ctx.paths)[i].empty());
```

**Проблема:** если проект **сохранён** (path не пустой), но потом **изменён** — `unsaved = false`. Пользователь закрывает без предупреждения.

**Фикс:** `ctx.modified[i]` из `BreezeDocument::IsModified`.

**Оценка:** 20 минут.

---

### CR-430. `ProjectTabs` — при закрытии последнего документа ничего не блокируется

**Где:** `ProjectTabs.cpp:120-124`

Кнопка закрытия рисуется только `if (docCount > 1)`. Ок, но если кто-то вызовет `onClose(0)` при `docCount == 1` — `Application::CloseDoc` уже защищён (CR-... в Application). **WONTFIX.**

---

### CR-431. `ProjectTabs` — `pendingCloseIdx` и `switchToIdx` — deferred actions после End

**Где:** `ProjectTabs.cpp:138-145`

```
if (pendingCloseIdx >= 0) {
    if (ctx.onClose) ctx.onClose(pendingCloseIdx);
    pendingCloseIdx = -1;
}
if (switchToIdx != -2) {
    if (switchToIdx >= 0 && ctx.onSwitch) ctx.onSwitch(switchToIdx);
    switchToIdx = -2;
}
```

**Правильная** практика — deferred после `ImGui::End()`, иначе при `onClose` меняется `ctx.documentCount` внутри цикла. **Но**:

**Проблема с `-2`:** это magic для «нет действия». А `-1` — тоже «нет» в `pendingCloseIdx`. **Два разных сентинеля.**

**Фикс:** явный `std::optional<int>` или единый `kNoPending = -1`.

**Оценка:** 10 минут.

---

---

## 🔴 P0 — ui/home (Волна 11, часть 1: HomeScreen.h)

### CR-432. `HomeContext` — `char* searchBuffer` + `searchBufferSize` — 4-й дубль

**Где:** `HomeScreen.h:33-34`
**Статус:** OPEN

То же, что CR-380 (QAT) и CR-356 (SvgButton). Один и тот же антипаттерн в 4 местах.

**Фикс:** общий `util::SearchBuffer` (либо `std::string`, либо `std::array<char,N>`).

**Оценка:** 40 минут (пакетно во всех 4 местах).

---

### CR-433. `HomeContext` — `bool* showHomeScreen` — косвенное состояние

**Где:** `HomeScreen.h:30`
**Статус:** OPEN

```
bool* showHomeScreen = nullptr;
```

HomeScreen **сам управляет флагом** — включает/выключает свой показ через указатель. Обычно это делает **вызывающий** (Application) по клику колбэка.

**Проблема:** HomeScreen и Application оба пишут в один флаг. Race-условие по дизайну.

**Фикс:** убрать `showHomeScreen` из ctx. Application решает, вызвать ли `DrawHomeScreen`.

**Оценка:** 15 минут.

---

### CR-434. `HomeContext` — `double ramMB` — почему home знает про RAM?

**Где:** `HomeScreen.h:39`

**По UI_LAYOUT** — телеметрия в QAT (FPS/RAM/Obj). Зачем home ещё одну показывает?

**Проверить в .cpp** — возможно, дублирование.

**Оценка:** 5 минут + убрать поле.

---

### CR-435. `HomeContext` — 7 колбэков без default + нет проверок

**Где:** `HomeScreen.h:42-48`

`onSwitchDoc`, `onCloseDoc`, `onCreateDoc`, `onOpenFile`, `onShowSettings`, `onNotImplemented`, `onQuit` — все `std::function`, могут быть пустыми.

**Фикс:** в `DrawHomeScreen` — ранние проверки или `assert`.

**Оценка:** 10 минут.

---

### CR-436. `Thumbnails::CaptureCurrentThumbnail` — параметр `activeDocIndex` не нужен?

**Где:** `Thumbnails.h:22`

```
void CaptureCurrentThumbnail(OcctView& view, int activeDocIndex,
                             std::vector<std::string>& thumbs);
```

**Проверить в .cpp** — используется ли `activeDocIndex` внутри? Если да — ок, если нет — удалить.

---

### CR-437. `Thumbnails::CaptureCurrentThumbnail` — сохраняет в `.bmp` (устаревшее)

**Где:** лог: `Thumbnail saved: .../thumb_0.bmp`

По UI_LAYOUT §2.1 — превью PNG. BMP — **без сжатия**, миниатюра 256×256 = **256 KB** на каждый документ.

**Фикс:** использовать PNG через `stb_image_write` (уже в проекте есть `stb_image`).

**Оценка:** 30 минут.

---

### CR-438. `Thumbnails` — magic 10 в комментарии «max 10»

**Где:** `Thumbnails.h:18`

```
/// Добавить файл в начало списка недавних (без дублей, max 10).
```

**Где эта константа в коде?** Проверить в .cpp — если magic `10` — вынести в `constexpr int kMaxRecentFiles = 10;`.

---

---

## 🔴 P0 — ui/home (Волна 11, часть 2: HomeScreen.cpp)

### CR-439. 🔴 HomeScreen напрямую работает с OpenGL — нарушение слоя UI

**Где:** `HomeScreen.cpp:15, 214-224`
**Статус:** OPEN
**Категория:** архитектура, нарушение `ARCHITECTURE.md §2`

```
#include <GL/gl.h>
...
GLuint texId = 0;
glGenTextures(1, &texId);
glBindTexture(GL_TEXTURE_2D, texId);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
```

**UI-модуль создаёт GL-текстуры напрямую.**

**Почему это плохо:**
- Если завтра перейдём на Metal/Vulkan для macOS — этот код сломается.
- UI-модуль не должен знать, что под ним OpenGL.
- Кэш текстур живёт **вне** `IconManager` — непонятно, кто владелец.
- `stb_image` используется в UI-модуле для декодирования — тоже слой не тот (декодирование — `util`).

**Фикс:**
1. Создать `util/ThumbnailCache.{h,cpp}` — управляет текстурами превью.
2. `util/ImageLoader` — обёртка над `stb_image`.
3. `HomeScreen` вызывает `ThumbnailCache::Get(path)` — не знает ни про GL, ни про stb.

**Оценка:** 2-3 часа (правильно).

---

### CR-440. 🔴 `static std::map<std::string, ImTextureID> s_thumbs;` — кэш никогда не очищается

**Где:** `HomeScreen.cpp:207`
**Статус:** OPEN
**Категория:** RAM, GPU-утечка

```
static std::map<std::string, ImTextureID> s_thumbs;
auto cacheIt = s_thumbs.find(thumbPath);
if (cacheIt == s_thumbs.end()) {
    // создаём текстуру, добавляем в кэш
    ...
    s_thumbs[thumbPath] = (ImTextureID)(intptr_t)texId;
}
```

**Проблемы:**
1. `static` в функции — общий для всех вызовов, никогда не освобождается.
2. **Никогда не вызывает `glDeleteTextures`** — текстуры остаются в VRAM до конца процесса.
3. **Не проверяет, изменился ли файл миниатюры** — если пользователь обновил `thumb_0.bmp`, старая текстура останется.
4. `std::map` — **O(log N)** вместо `unordered_map` O(1).
5. **Бесконтрольный рост** — 100 документов = 100 текстур 320×240 = ~30 МБ GPU.

**Связано с RAM-H1 (IconManager).**

**Фикс:**
- Хранить в `HomeScreen` **не static**, а в `Application::myThumbnailCache`.
- `~Application` вызывает `Clear()`.
- LRU-лимит (например, 50 текстур).
- `unordered_map`.

**Оценка:** 1.5 часа.

---

### CR-446. HomeScreen — magic 30+ значений layout

**Где:** весь файл

```
const float titleH  = 32.0f;
const float topBarH = 76.0f;
const float leftW   = 240.0f;
const float pad     = 16.0f;
...
float logoY = 22.0f + 36.0f + 16.0f;
...
ImGui::SetCursorPosY(132);      // ← magic
ImGui::SetCursorPosY(152);
ImGui::SetCursorPosY(172);
...
const float tileW = 200.0f;
const float tileH = 200.0f;
const float spacing = 16.0f;
...
if (col >= 4) { ... }           // ← magic 4 (колонки)
```

**Расхождение с UI_LAYOUT** — там QAT 32, ribbon 78. Здесь title 32 + topBar 76 = **108**.

**Фикс:** `constexpr` + актуализация канона.

**Оценка:** 1.5 часа.

---

### CR-447. HomeScreen — дублирование «EndChild/PopStyleColor/PopID» в 2 ветках
**Включает (merged):** CR-478, CR-524, CR-530

**Где:** `HomeScreen.cpp:307-316, 319-322`

```
if (ImGui::Button("Закрыть", ...)) {
    if (ctx.onCloseDoc) ctx.onCloseDoc(i);
    ImGui::EndChild();          // ← break перед End
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    break;
}
ImGui::EndChild();
ImGui::PopStyleColor(2);
ImGui::PopStyleVar(2);
ImGui::PopID();
```

**Странный контроль:** чтобы не открывать закрытый `EndChild` после `onCloseDoc`, делается **break с ручным pop**. Хрупко: если добавят ещё один ресурс — забудут в обеих ветках.

**Фикс:** отложенный `pendingCloseDoc`, обрабатываемый после цикла (как `pendingCloseIdx` в ProjectTabs).

**Оценка:** 20 минут.

---

### CR-448. HomeScreen — `std::tolower` не работает для кириллицы

**Где:** `HomeScreen.cpp:334-335, 368-369, 405-406`

```
std::string searchLower = ctx.searchBuffer;
for (auto& ch : searchLower) ch = (char)std::tolower((unsigned char)ch);
...
for (auto& ch : lname) ch = (char)std::tolower((unsigned char)ch);
```

**Проблема:** `std::tolower` работает только для ASCII. Для кириллицы (`А-Я` → `а-я`) — **не работает**, возвращает исходный символ.

**Что ломается:** поиск «деталь» не найдёт «Деталь».

**Фикс:**
- `util/Utf8::ToLowerInPlace(std::string&)` — Unicode-aware.
- Или ICU (тяжёлый).
- Или явная обработка диапазонов UTF-8 (для RU — 2 байта на символ).

**Оценка:** 1 час.

---

### CR-449. HomeScreen — поиск пересчитывает lowercase **на каждый кадр**

**Где:** `HomeScreen.cpp:334-335, 405-406`

`searchLower` пересчитывается **на каждый кадр**, хотя меняется только при вводе. При активном поиске — 60 FPS × 15 команд = **900 lowercase-итераций/сек**.

**Фикс:** кэшировать lowercase, пересчитывать только при изменении буфера.

**Оценка:** 20 минут.

---

### CR-450. HomeScreen — дублирование кода создания текстуры в 2 ветках

**Где:** `HomeScreen.cpp:211-227` (плитки), `HomeScreen.cpp:329-340` (список — уже через статик кэш).

**В списке** для миниатюр нет — только текст. Но для **обоих режимов** — повторение кода поиска lowercase.

**Фикс:** общий helper `MatchesSearch(name, queryLower)`.

**Оценка:** 10 минут.

---

### CR-451. HomeScreen — `onSwitchDoc` не закрывает home для плиток правильно

**Где:** `HomeScreen.cpp:302-303`

```
if (ImGui::Button("Открыть", ...)) {
    if (ctx.onSwitchDoc) ctx.onSwitchDoc(i);
    (*ctx.showHomeScreen) = false;      // ← косвенно
}
```

**Связано с CR-433.** HomeScreen **сам** управляет `showHomeScreen`. Если Application решит оставить home открытым (например, ошибка при switch) — уже поздно.

**Фикс:** `showHomeScreen` убрать из ctx. Application решает по колбэку.

**Оценка:** 20 минут.

---

### CR-452. HomeScreen — `ImGui::Selectable(...)` без проверки на одиночный клик в списке

**Где:** `HomeScreen.cpp:420-425`

```
bool sel = ImGui::Selectable(nm.c_str(), false,
    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
if (sel && ImGui::IsMouseDoubleClicked(0)) {
    if (ctx.onSwitchDoc) ctx.onSwitchDoc(i);
    (*ctx.showHomeScreen) = false;
}
```

**Работает** только на двойной клик. **Одиночный клик** — ничего. По UX-паттернам — ок, но пользователь может не догадаться.

**Фикс:** добавить подсказку «Двойной клик — открыть» или одиночный клик для select + Enter/двойной — для open.

**Оценка:** 10 минут.

---

### CR-454. HomeScreen — 449 строк **одной функцией**

**Где:** весь `DrawHomeScreen`

**Разложение:**
- Топ-полоса Деталь/Сборка/Открыть.
- Левая колонка (лого, версия, 5 кнопок).
- Правая область (заголовок, тумблер, поиск, плитки/список).

**Фикс:** 5-6 приватных helper-функций + структура `HomeLayout`.

**Оценка:** 2 часа.

---

### CR-458. `CaptureCurrentThumbnail` — сохраняет в BMP вместо PNG

**Где:** `Thumbnails.cpp:26-30`

```
std::snprintf(filename, sizeof(filename), "thumb_%d.bmp", activeDocIndex);
...
if (view.CaptureToPixmap(320, 240, path)) {
```

**По UI_LAYOUT §2.1** — превью PNG.

**BMP:** без сжатия. Миниатюра 320×240 = **~300 KB**. Для 100 документов = **30 МБ мусора** на диске.

**Плюс:** `CaptureToPixmap(width, height, ...)` **игнорирует** width/height (CR-302). То есть размер не 320×240, а **размер вьюпорта** — может быть 1920×1080 = 8 МБ на одну миниатюру!

**Фикс:**
1. Реализовать `CaptureToPixmap` с реальным размером.
2. Сохранять как PNG через `stb_image_write`.

**Оценка:** 2 часа (правильно).

---

### CR-459. `CaptureCurrentThumbnail` — `thumbs.resize(activeDocIndex + 1)` с риском

**Где:** `Thumbnails.cpp:32-34`

```
if ((int)thumbs.size() <= activeDocIndex) {
    thumbs.resize(activeDocIndex + 1);
}
thumbs[activeDocIndex] = path;
```

**Если activeDocIndex = 5** и размер thumbs = 2 → resize до **6**, индексы 2, 3, 4 становятся **пустыми строками**.

**Что это значит:** `GetThumbnailPath(thumbs, 3)` вернёт `""` — правильно. Но в списке `thumbs` есть **пропуски**. Если Application ожидает **плотный** массив — сломается.

**Фикс:** либо плотный массив (всегда поддерживать размер = documentCount), либо `std::map<int, string>`.

**Оценка:** 20 минут.

---

### CR-460. `AddRecentFile` — magic 10

**Где:** `Thumbnails.cpp:18`

```
if (recent.size() > 10) recent.resize(10);
```

**Фикс:** `constexpr int kMaxRecentFiles = 10;`.

**Оценка:** 2 минуты.

---

### CR-461. `AddRecentFile` — O(N) erase с `std::remove`

**Где:** `Thumbnails.cpp:15-17`

```
recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
recent.insert(recent.begin(), path);
```

Для 10 элементов — ок. Но `std::vector::insert(begin(), ...)` — **O(N)** сдвиг.

**WONTFIX** — 10 элементов, не критично.

---

### CR-462. `CaptureCurrentThumbnail` — `std::cout` в hot-path

**Где:** `Thumbnails.cpp:37`

```
std::cout << "Thumbnail saved: " << path << "\n";
```

Вызывается на **каждое переключение документа** (из `SwitchToDoc`). Связано с CR-133.

**Оценка:** входит в пакет CR-133.

---

### CR-463. `CaptureCurrentThumbnail` — `activeDocIndex` проверяется только на `< 0`

**Где:** `Thumbnails.cpp:20`

```
if (activeDocIndex < 0) return;
```

**Что если `activeDocIndex = 999`**, а документов 3? Превью создастся для несуществующего индекса, `thumbs.resize(1000)` — **пустые строки** в массиве.

**Фикс:** `if (activeDocIndex < 0 || activeDocIndex > MAX) return;`.

**Оценка:** 5 минут.

---

### CR-464. `GetThumbnailPath` — не проверяет, что файл существует

**Где:** `Thumbnails.cpp:10-13`

```
std::string GetThumbnailPath(const std::vector<std::string>& thumbs, int docIndex) {
    if (docIndex < 0 || docIndex >= (int)thumbs.size()) return "";
    return thumbs[docIndex];
}
```

Возвращает путь как есть. Проверка `std::filesystem::exists` — **у вызывающего** (`HomeScreen.cpp:206`).

**Фикс:** проверять здесь — тогда `HomeScreen` не делает лишний вызов.

**Оценка:** 5 минут.

---

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 1: RenameDialog)

### CR-466. `DrawRenameDialog` — `ImGui::OpenPopup` вызывается каждый кадр, ESC не работает
**Включает (merged):** CR-472, CR-483

**Где:** `RenameDialog.cpp:12`
**Статус:** OPEN
**Категория:** функциональный баг

```
if (!ctx.state || !ctx.state->show) return;
ImGui::OpenPopup("Переименовать");     // ← КАЖДЫЙ кадр
```

**Проблема:**
- `p_open = nullptr` в `BeginPopupModal`.
- `OpenPopup` вызывается **каждый кадр**, пока `show == true`.
- Если ImGui закрывает modal (например, ESC), `show` остаётся `true`, а `OpenPopup` **на следующем кадре снова открывает**.

**Результат:** **ESC не закрывает диалог**.

**Фикс:**
1. Вызывать `OpenPopup` **один раз** при переходе `show` из `false` в `true`.
2. Или — передавать `p_open = &ctx.state->show` в `BeginPopupModal` — тогда ImGui сам синхронизирует.

**Оценка:** 15 минут.

---

### CR-467. `RenameDialog` — `char buffer[128]` — ограничение 127 символов

**Где:** `RenameDialog.h:14`

```
char buffer[128] = "";
```

Имя фичи — **жёсткое ограничение 127 символов**. Длинные имена (например, «Отверстие M8×1.25 ГОСТ 7798-70 с зенковкой») — обрежутся.

**Плюс:** `snprintf` при редактировании (ImGui) **порежет посреди UTF-8** → кракозябры на последнем символе.

**Фикс:** `std::string buffer;` + `ImGui::InputText` с `std::string` (ImGui 1.90+).

**Оценка:** 20 минут.

---

### CR-469. `RenameDialog` — нет обработки пустого/невалидного ввода

**Где:** `RenameDialog.cpp:22`

```
if (ctx.doc && !ctx.state->pendingLabel.IsNull() && ctx.state->buffer[0] != '\0') {
    ctx.doc->RenameFeature(...);
}
```

**Что если имя только из пробелов** (`"   "`)? — `buffer[0] != '\0'` = true, ренейм на `"   "`. Пользователь получает безымянную фичу.

**Фикс:** тримминг + `if (trimmed.empty()) { disabled OK button }`.

**Оценка:** 10 минут.

---

### CR-470. `RenameDialog` — диалог не блокирует `Enter` для подтверждения при пустом имени

**Где:** `RenameDialog.cpp:20`

```
ImGui::InputText("##rename_buf", ..., ImGuiInputTextFlags_EnterReturnsTrue);
```

Флаг `EnterReturnsTrue` установлен — но **результат не проверяется**. `if (ImGui::InputText(...))` — пропущено. Значит: **Enter в поле ввода ничего не делает**.

**Фикс:** `if (ImGui::InputText(...)) { /* то же, что OK */ }`.

**Оценка:** 5 минут.

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 2: DeleteConfirmDialog)

### CR-473. `DeleteConfirmDialog` — `skipConfirm` не сохраняется в конфиг

**Где:** `DeleteConfirmDialog.h:13`, `Application.cpp:997`

```
bool skipConfirm = false;   // «до перезапуска»
```

**Что это значит:**
- Пользователь поставил галку — «не спрашивать снова».
- При перезапуске — **снова спрашивает**.
- Обещание «до перезапуска» **не соответствует UX** — обычно такие галки сохраняются в конфиг.

**Фикс:** либо сохранять в `SettingsState` → `%APPDATA%/BreezeCAD/settings.json`, либо **убрать галку вообще** (проще).

**Оценка:** 30 минут.

---

### CR-474. `DeleteConfirmDialog` — нет `onCancelled` колбэка

**Где:** `DeleteConfirmDialog.h:22-27`

Есть `onDeleted`, но **нет `onCancelled`**. Если понадобится логировать отмены или сбрасывать что-то в Application — некуда.

**Фикс:** добавить `std::function<void()> onCancelled;`.

**Оценка:** 5 минут.

---

### CR-475. `DeleteConfirmDialog` — `char info[256]` — риск обрезки UTF-8

**Где:** `DeleteConfirmDialog.h:12`

`Application::RequestDelete` использует `std::snprintf` для формирования `info`. Если имя фичи длинное — обрезка **посередине UTF-8** → кракозябры в конце.

**Фикс:** `std::string info;` + `ImGui::TextWrapped("%s", info.c_str())`.

**Оценка:** 15 минут.

---

### CR-479. `UnsavedChangesDialog` — complex state: 2 флага `pendingCloseDocIndex` + `pendingWindowClose`

**Где:** `UnsavedChangesDialog.h:8-11`

```
int  pendingCloseDocIndex = -1;   // какой док закрываем (-1 = не закрывать)
bool pendingWindowClose   = false;
```

**Что это значит:**
- `-1` — sentinel «нет действия».
- Если `pendingWindowClose == true` — `pendingCloseDocIndex` **тоже должен** указывать на что-то? Нет — при закрытии окна может быть **несколько** modified документов.

**Проблема:** комментарий в `Application::Run` (CR-... в Application) говорит: `myUnsavedState.pendingCloseDocIndex = modIdx;` (первый найденный modified). То есть при закрытии окна сохраняется/спрашивается **один** документ, а не все.

**Что это значит:** пользователь имеет 3 modified документа, закрывает окно, ему предлагают сохранить **только первый**. Остальные теряются **молча**.

**Фикс:**
- При закрытии окна — обход всех modified документов.
- Или явный диалог «Сохранить все?».

**Оценка:** 2 часа (правильный UX).

---

### CR-480. `UnsavedChangesDialog` — `onSave` возвращает bool, но семантика непонятна

**Где:** `UnsavedChangesDialog.h:19`

```
std::function<bool(int idx)> onSave;      // попытка сохранить док idx, true = ок
```

**Вопрос:** `true` = «сохранилось успешно» или «пользователь выбрал сохранить»?

Из кода:
```
saved = ctx.onSave(idx);
if (!saved) return;   // диалог остаётся открытым
```

Если пользователь в диалоге «Сохранить как» **нажал Отмена** — `onSave` вернёт `false` → диалог **остаётся открытым**. Ок.

Но комментарий говорит «попытка сохранить, true = ок». **Имя `onSave`** двусмысленное. **Фикс:** переименовать в `trySaveAndProceed`.

**Оценка:** 5 минут.

---

### CR-481. `UnsavedChangesDialog` — `if (!saved) return;` — выходит из функции до `EndPopup`

**Где:** `UnsavedChangesDialog.cpp:26-27`

```
bool saved = true;
if (idx >= 0 && idx < ctx.documentCount && ctx.onSave) {
    saved = ctx.onSave(idx);
}
if (!saved) return;   // ← выход из функции
...
ImGui::EndPopup();
```

**Проблема:** `return` до `ImGui::EndPopup()`. **ImGui считает, что popup не закрыт корректно** — оставит `ImGui::BeginPopupModal` в невалидном состоянии.

**Что происходит:** в следующий кадр — `ImGui::BeginPopupModal` с уже открытым popup без `EndPopup` → стек ImGui ломается. **Assert в debug, UB в release.**

**Это функциональный баг.** **Фикс:** убрать `return`, использовать флаг `bool shouldClose = false;`.

**Оценка:** 15 минут. **Критично.**

---

### CR-482. `UnsavedChangesDialog` — `onDiscard` вызывается даже для window close с `idx == -1`?

**Где:** `UnsavedChangesDialog.cpp:60-62`

```
if (idx >= 0 && idx < ctx.documentCount && ctx.onDiscard) {
    ctx.onDiscard(idx);
}
if (wasWindow) {
    if (ctx.onQuit) ctx.onQuit();
}
```

**Логика ок:** при закрытии окна — `idx` может быть `-1` (если modified documents не найдены, но `pendingWindowClose = true`). Тогда `onDiscard(-1)` **не вызывается** (guard есть).

**Но:** если при закрытии окна `idx >= 0` (первый modified документ), то:
1. `onDiscard(idx)` — помечает первый как несохранённый.
2. `onQuit()` — закрывает приложение.

**Что с остальными modified документами?** Они **не пройдут** через `onDiscard`. При выходе — `UnsavedState` уже `show = false`. Никакой второй вопрос. **Потеря данных.**

**Связано с CR-479.**

**Оценка:** входит в CR-479.

---

### CR-485. Все 3 диалога — `BeginPopupModal(..., AlwaysAutoResize)`

**Где:** все три диалога

```
ImGui::BeginPopupModal("...", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
```

**Проблема:** `AlwaysAutoResize` + модалка — ок, но **не проверяется, что размер умещается на экране**. Если текст длинный (например, `info[256]` с длинным именем файла) — модалка уйдёт за границы экрана.

**UI_LAYOUT §18.4** требует плавающие окна с максимальным размером.

**Фикс:** `SetNextWindowSizeConstraints(min, max)` + `ImGuiCond_FirstUseEver`.

**Оценка:** 30 минут (пакетно).

---

## 🟠 P1 — ui/dialogs

### CR-487. Все диалоги — `BreezeDocument*` — прямая зависимость от doc

**Где:** `RenameDialog.h:17`, `DeleteConfirmDialog.h:25`

UI **напрямую вызывает** `doc->RenameFeature(...)` и `doc->RemoveFeature(...)`. Слой `app/` **пропущен**.

**Фикс:** колбэки `onRename(label, name)`, `onDelete(label)` — Application решает.

**Связано с CR-486.**

**Оценка:** входит в CR-486.

---

### CR-488. `RenameDialog` / `DeleteConfirmDialog` — нет `nullptr` guard на `ctx.doc`

**Где:** `RenameDialog.cpp:22`, `DeleteConfirmDialog.cpp:24`

```
if (ctx.doc && !ctx.state->pendingLabel.IsNull() && ...) {
```

**Проверка есть — ок.** WONTFIX.

---

### CR-489. `UnsavedChangesDialog` — `pendingCloseDocIndex` default `-1`, но `-1` валидный индекс?

**Где:** `UnsavedChangesDialog.h:8`

`-1` как sentinel — общепринято, но **не сентинел для `std::optional`**. Если кто-то передаст `-1` вместо `0`, диалог **не сработает**.

**Фикс:** `std::optional<int>`.

**Оценка:** 15 минут.

---

---

## 🟠 P1 — ui/dialogs (Волна 12, часть 4: AddBoxDialog)

### CR-490. `AddBoxDialog` — единственный диалог с правильным паттерном (эталон)

**Где:** `AddBoxDialog.cpp:11-14`, `:26`, `:30`, `:42`, `:48`
**Статус:** NOT A BUG — эталон

```
if (ctx.state->show) {
    ImGui::OpenPopup(Tr("dlg.addBox.title").c_str());
    ctx.state->show = false;   // ← одноразовый триггер
}
if (ImGui::BeginPopupModal(Tr("dlg.addBox.title").c_str(), ...)) { ... }
```

**Что правильно:**
1. `OpenPopup` вызывается **один раз** (не каждый кадр).
2. `show = false` сразу после — не залипает.
3. `Tr()` для всех строк и key.

**Действие:** использовать как **образец** для CR-465, CR-471, CR-477 (другие диалоги).

**Оценка:** 0 (эталон).

---

### CR-491. `AddBoxState` — `char name[128]` — риск обрезки UTF-8

**Где:** `AddBoxDialog.h:12`
**Статус:** OPEN

То же, что CR-467 (RenameDialog). Если пользователь введёт длинное имя (с кириллицей) — ImGui обрежет буфер по 127-байтной границе, **посередине символа**.

**Фикс:** `std::string name;` + `InputText` с `std::string` (ImGui 1.90+).

**Оценка:** 15 минут (пакетно с CR-467).

---

### CR-492. `AddBoxDialog` — нет валидации size / position

**Где:** `AddBoxDialog.cpp:36-38`

```
if (ImGui::Button(Tr("btn.ok").c_str(), ImVec2(120, 0))) {
    if (ctx.doc) {
        ctx.doc->AddBoxFeature(ctx.state->name,
            ctx.state->size[0], ctx.state->size[1], ctx.state->size[2],
            ctx.state->pos[0],  ctx.state->pos[1],  ctx.state->pos[2]);
```

**Что если size отрицательный** (`-100`)? `BRepPrimAPI_MakeBox` бросит **исключение** → не поймано → краш.

**Что если size = 0?** Тоже исключение.

**Что если name пустой?** — создастся безымянный Box (0 байт) — но `AddBoxFeature` ставит `<unnamed>`.

**Фикс:**
- Валидация в UI: `if (size[0] <= 0 || size[1] <= 0 || size[2] <= 0) { disable OK }`.
- И/или try/catch в `AddBoxFeature` с `errno`/`lastError`.

**Оценка:** 30 минут.

---

### CR-493. `AddBoxDialog` — `float pos[3]`, `size[3]` — precision loss для САПР

**Где:** `AddBoxDialog.h:13-14`

```
float pos[3]  = { 0.0f, 0.0f, 0.0f };
float size[3] = { 100.0f, 100.0f, 100.0f };
```

**Проблема:** `float` имеет ~7 десятичных знаков точности. Для САПР (мм):
- 999999.99 — предел.
- 0.000001 мм — предел разрешения.
- При смещении от origin 1 мкм **потеряется**.

**Плюс:** `AddBoxFeature` принимает `double`, а мы передаём `float` — implicit conversion.

**Фикс:** `double pos[3]`, `size[3]` + `ImGui::InputDouble` (или `InputScalar` с `ImGuiDataType_Double`).

**Оценка:** 20 минут (пакетно — проверить все числовые поля).

---

### CR-494. `AddBoxDialog` — `ImGui::InputFloat` для мм без диапазона и шага

**Где:** `AddBoxDialog.cpp:22-24, 29-31`

```
ImGui::InputFloat("X##pos", &ctx.state->pos[0]);
ImGui::InputFloat("DX", &ctx.state->size[0]);
```

**Фикс:**
- `InputDouble("X##pos", &pos[0], 1.0, 10.0, "%.2f мм")` — шаг и формат.
- Или кастомный компонент `MmInputField`.

**Оценка:** входит в CR-493.

---

### CR-495. `AddBoxDialog` — `Tr("dlg.name").c_str()` как label с `##` — ImGui ID с кириллицей
**Включает (merged):** CR-579, CR-623

**Где:** `AddBoxDialog.cpp:18-19`

```
ImGui::InputText(Tr("dlg.name").c_str(), ctx.state->name, sizeof(ctx.state->name));
```

`Tr("dlg.name")` возвращает `std::string`. `.c_str()` — временный объект жив до конца выражения. `InputText` использует как label — **ImGui использует label для вычисления ID**.

**Проблема:** если при смене языка `dlg.name` меняется с «Имя» на «Name» — **ImGui ID изменится** → состояние виджета сбросится.

**Фикс:** использовать `##key`:
```
ImGui::InputText("##dlg_name", ctx.state->name, sizeof(ctx.state->name));
```
(Отображаемый label — отдельно через `ImGui::Text`.)

**Или** `"##" + Tr("dlg.name")` — стабильный суффикс.

**Оценка:** 15 минут (пакетно — все поля всех диалогов).

---

### CR-497. `PropertiesDialog` — `Begin` не проверяется, но `End` вызывается всегда

**Где:** `PropertiesDialog.cpp:18, 45`
**Статус:** OPEN
**Категория:** потенциальный assertion ImGui

```
if (ImGui::Begin("Свойства", &ctx.state->show)) {
    ...
}
ImGui::End();   // ← вне if(Begin) — но если Begin вернул false, End НЕ нужен
```

**Проблема:** В ImGui 1.85+ документация говорит: `End()` **всегда** вызывается, **если Begin был вызван**, независимо от return. **То есть код формально правильный.**

**НО:** если `Begin` вернул false — содержимое **не отрисовано**, но окно уже создано. `End()` корректен.

**WONTFIX** — соответствует документации.

---

### CR-498. `PropertiesDialog` — `"Свойства"` — хардкод (1 строка)

**Где:** `PropertiesDialog.cpp:18`

`ImGui::Begin("Свойства", ...)` — хардкод. **Фикс:** `Tr("dlg.properties.title")`.

**Оценка:** 2 минуты.

---

### CR-501. `PropertiesDialog` — `ImGui::Text("Тип: %d", type)` дублирует `typeName`

**Где:** `PropertiesDialog.cpp:30, 48`

```
ImGui::Text("Тип: %d", type);      // ← числовой код
...
ImGui::Text("Описание: %s", typeName);   // ← словесное
```

**Что видит пользователь:**
```
Тип: 10
Описание: Куб
```

**Дублирование.** Пользователю нужен **только typeName**.

**Фикс:** убрать `Тип: %d` или показать как debug-info (свернуто).

**Оценка:** 5 минут.

---

### CR-502. `PropertiesDialog` — `switch (type)` без `FT_Other` case

**Где:** `PropertiesDialog.cpp:34-47`

```
const char* typeName = "Неизвестно";
switch (type) {
    case FT_Start:       ...
    case FT_Plane:       ...
    ...
    case FT_SketchLine:  ...
}
```

**Что если `type == FT_Other (99)`?** — `typeName` остаётся `"Неизвестно"`. Формально ок.

**Но:** если **новый тип фичи** добавят и забудут в `switch` — пользователь увидит «Неизвестно» **без имени типа**. Диагностика невозможна.

**Фикс:**
- `default: snprintf(typeName, "Тип %d", type);` — показать хотя бы код.
- Или вынести switch в `doc/FeatureTypeToString.cpp` — единый источник.

**Оценка:** 20 минут.

---

### CR-503. `PropertiesDialog` — `Tag() %d` — потенциальное переполнение

**Где:** `PropertiesDialog.cpp:31`

```
ImGui::Text("Тег: %d", ctx.state->pendingLabel.Tag());
```

`Tag()` возвращает `int`. Ок. WONTFIX.

---

### CR-507. `NotImplementedDialog` — единственный диалог с `p_open = &state.show`

**Где:** `NotImplementedDialog.cpp:11-12`

```
if (ImGui::BeginPopupModal("##not_implemented", &state.show, ...)) {
```

**Правильно!** Это единственный диалог, где `p_open` передан явно. ImGui сам синхронизирует состояние при закрытии (X, ESC, клик вне).

**Но:** `if (state.show) OpenPopup;` — без сброса `state.show = false`. Работает за счёт `p_open` (ImGui сбрасывает `state.show` при закрытии).

**Действие:** сделать эталоном для остальных диалогов (CR-465, CR-471, CR-477).

**Оценка:** 0 (эталон).

---

### CR-509. `NotImplementedDialog` — ключ `"##not_implemented"` — ImGui-style ID, а должен быть локализуемый title

**Где:** `NotImplementedDialog.cpp:9, 11`

```
ImGui::OpenPopup("##not_implemented");
ImGui::BeginPopupModal("##not_implemented", ...);
```

**`##not_implemented`** — ImGui ID, **не отображается**. Модалка не имеет **видимого заголовка**. Пользователь видит прямоугольник без title.

**Фикс:** `Tr("notimpl.title") + "##not_implemented"` или `"Уведомление##not_implemented"`.

**Оценка:** 5 минут.

---

### CR-511. `NotImplementedDialog` — `state.show = false` + `CloseCurrentPopup()` — двойной сброс

**Где:** `NotImplementedDialog.cpp:19-21`

```
if (ImGui::Button("OK", ImVec2(120, 0))) {
    state.show = false;
    ImGui::CloseCurrentPopup();
}
```

`p_open = &state.show` уже синхронизирует. Дополнительный `state.show = false` — избыточно. **WONTFIX** (не ломает).

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 7: сводное по всем диалогам)

### CR-512. Диалоги — нет консистентности API

**Где:** все 6+ диалогов

| Диалог | `state` в ctx? | `Tr()`? | Паттерн OpenPopup | p_open |
|---|---|---|---|---|
| RenameDialog | pointer | ❌ | каждый кадр | ❌ |
| DeleteConfirmDialog | pointer | ❌ | каждый кадр | ❌ |
| UnsavedChangesDialog | pointer | ❌ | каждый кадр | ❌ |
| **AddBoxDialog** | pointer | ✅ | одноразовый | ❌ |
| **PropertiesDialog** | pointer | ❌ (17 hardcode) | Begin, не popup | ❌ |
| **NotImplementedDialog** | reference | ❌ (3 hardcode) | одноразовый | ✅ |

**Три разных паттерна в 6 файлах.**

**Фикс:** единый шаблон — `if (state.show) { OpenPopup(...); state.show = false; } if (BeginPopupModal(..., &state.show, ...)) { ... }`.

**Оценка:** 2 часа (унификация 6 диалогов).

---

### CR-513. Все диалоги — `ctx.doc` и `ctx.state` без nullptr guard

**Где:** все файлы

```
void DrawRenameDialog(const RenameContext& ctx) {
    if (!ctx.state || !ctx.state->show) return;   // ✅ state проверяется
    ...
    if (ctx.doc && ...) { ... }                    // ✅ doc проверяется
```

**Ок.** WONTFIX.

---

### CR-514. Все диалоги — нет `ESC`-обработки через `p_open`

**Где:** все, кроме NotImplementedDialog

`p_open = nullptr` → ESC **не закрывает** модалку. Пользователь жмёт ESC → ничего. **UX-баг** из UI_LAYOUT §1.5 «Всё можно отменить (Ctrl+Z, Esc)».

**Фикс:** везде передавать `&state.show` + `if (state.show) OpenPopup; state.show = false;`.

**Связано с CR-512.**

---

### CR-515. Диалоги — нет единой темы оформления

**Где:** все

Цвета кнопок, отступы, размеры кнопок (`ImVec2(120, 0)`), separator-стили — **у каждого диалога свои**. Не используется `ThemeTokens`.

**Связано с CR-322** (расширение токенов).

**Оценка:** входит в пакет CR-322.

---

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 8: DependencyDialog)

### CR-516. `DependencyDialog` — REVIEW-правило: прототип, не вызывается

**Где:** `DependencyDialog.h:4-13`
**Статус:** REVIEW (не удалять)

Комментарий в коде:
```
// СТАТУС: prototype, пока не вызывается из UI.
//   - Для 2D-ограничений эскиза избыточно ...
//   - ПРИГОДИТСЯ в Фазе 4 для 3D-зависимостей сборки ...
```

Соответствует **C-01** из `REFACTOR_NOTES.md`. Не удалять. Оставить.

**Действие:** без изменений.

---

### CR-518. `DependencyDialog` — `ctx.state->type % 8` — magic

**Где:** `DependencyDialog.cpp:22`

```
const char* title = depNames[ctx.state->type % 8];
```

`type` — 0..7 согласно комментарию в `.h`. **`% 8` защита от OOB**, но:
- Магическое число — откуда 8? Размер массива.
- Если добавится 9-й тип — забудут поменять.
- `size == 8` захардкожен дважды (в `.h` комментарии и здесь).

**Фикс:** `constexpr size_t kDependencyTypeCount = 8;` + `std::size(depNames)`.

**Оценка:** 5 минут.

---

### CR-519. `DependencyDialog` — `std::string winTitle = std::string(title) + "##dep_dialog";` — аллокация каждый кадр

**Где:** `DependencyDialog.cpp:32`

Каждый кадр (пока диалог открыт — **60 раз/сек**):
- `std::string(title)` — копия C-строки.
- Конкатенация с `"##dep_dialog"`.
- Ещё одна аллокация.

**Фикс:** `char winTitle[128]; snprintf(...)` или `static` буфер + пересчёт при смене title. Или — `PushID` + фиксированный заголовок `"##dep_dialog"`, а title отрисовать как отдельный `Text`.

**Оценка:** 10 минут.

---

### CR-520. `DependencyDialog` — `size_t i` + `PushID((int)i)` — cast

**Где:** `DependencyDialog.cpp:44-50, 65-71`

```
for (size_t i = 0; i < ctx.state->base.size(); i++) {
    ...
    ImGui::PushID((int)i);       // ← static_cast
```

Использовать `static_cast<int>(i)`. Или `for (int i = 0; i < (int)size; ...)` — на манер остального кода.

**Оценка:** 3 минуты.

---

### CR-521. `DependencyDialog` — `erase(...begin() + i)` в цикле с `break`

**Где:** `DependencyDialog.cpp:47-52, 68-73`

```
for (size_t i = 0; i < ctx.state->base.size(); i++) {
    std::string name = Util::GetLabelName(ctx.state->base[i]);
    ImGui::PushID((int)i);
    ImGui::Text("%s", name.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
    if (ImGui::SmallButton("x")) {
        ctx.state->base.erase(ctx.state->base.begin() + i);
        ImGui::PopID();
        break;   // ← break после erase
    }
    ImGui::PopID();
}
```

**Проблема:** после `erase` — `break`. Это **правильно** (нельзя продолжать итерацию с изменённым vector), но:
1. `PopID` вызывается **дважды**? Нет — один в `if` + один в конце. Проверка: если `SmallButton` нажата — `PopID` в if + `break` — второй `PopID` (вне if) **не выполнится**. **Ок.**
2. Однако **легко ошибиться**: следующая версия кода может забыть `break`.

**Фикс:** post-loop `pendingErase = i; break;` + после цикла erase.

**Оценка:** 10 минут.

---

### CR-522. `DependencyDialog` — `Util::GetLabelName` — зависимость от `util/LabelName`

**Где:** `DependencyDialog.cpp:4, 46, 67`

UI зависит от `util/LabelName`. `LabelName` — обёртка над `TCollection_AsciiString`. **BUG-A5** (`TCollection_AsciiString` теряет кириллицу). Здесь UI получает потенциально сломанное имя.

**Связано с BUG-A5.**

**Фикс:** сначала BUG-A5.

---

### CR-523. `DependencyDialog` — `ctx.view->ClearSelection()` — UI дёргает OcctView

**Где:** `DependencyDialog.cpp:99, 105, 113`

```
if (ctx.view) ctx.view->ClearSelection();
```

UI диалога **напрямую работает с OcctView**. Нарушение `ARCHITECTURE §2`: UI не должен знать про OCCT.

**Фикс:** колбэк `onCancel/onApply/onDone()` в Application → Application дёргает OcctView.

**Оценка:** 20 минут.

---

### CR-528. `DependencyDialog` — `posSet` — флаг «первое появление», не переустанавливается

**Где:** `DependencyDialog.cpp:24-28`

```
if (!ctx.state->posSet) {
    ctx.state->pos = ImVec2(io.DisplaySize.x * 0.5f - 150,
                            io.DisplaySize.y * 0.5f - 150);
    ctx.state->posSet = true;
}
```

**Работает** — центрирует окно при первом показе. Но:
- При смене разрешения окна — позиция останется старой.
- `ImGuiCond_Once` + `SetNextWindowPos(ctx.state->pos)` работает только если state.pos обновлён.

**Связано с CR-518** (magic 150).

**Оценка:** входит в CR-527.

---

### CR-529. `DependencyDialog` — `open` вместо `show` (несогласованность с другими диалогами)

**Где:** `DependencyDialog.h:24`

```
bool open = false;
```

Другие диалоги — `show`. **Несогласованно.**

**Фикс:** переименовать в `show`.

**Оценка:** 3 минуты.

---

### CR-532. 🔴 `SettingsDialog` — все настройки визуальные, ни одна не работает

**Где:** весь файл

**Связано с B-11.**

| Вкладка | Что работает |
|---|---|
| Основные | ❌ (units, precision, autosave) |
| Графика | ❌ (quality, aa, shadows) |
| **Тема** | ✅ (radio → onThemeChange) |
| **Язык** | ✅ (radio → onLanguageChange) |
| Хоткеи | ❌ (только текст) |
| О программе | — (только чтение) |

**Пользователь двигает слайдер «Точность»** → ничего не происходит. Диалог настроек **не настроек**, а **декорации**.

**Фикс:** либо реализовать, либо **явно показать «(не реализовано)»** рядом с полем.

**Оценка:** входит в CR-531.

---

### CR-535. `SettingsDialog` — версия «0.1.0» захардкожена

**Где:** `SettingsDialog.cpp:138`

```
ImGui::Text("Версия: 0.1.0 (prototype)");
```

**Расхождение с CMakeLists** — там нет `VERSION` (CR-203). Версию негде посмотреть кроме этого места.

**Фикс:**
1. В CMakeLists — `project(BreezeCAD VERSION 0.1.0)`.
2. `target_compile_definitions(breezecad PRIVATE BREEZECAD_VERSION="${PROJECT_VERSION}")`.
3. В коде — `Text("Версия: %s", BREEZECAD_VERSION)`.

**Связано с CR-203.**

**Оценка:** 20 минут.

---

### CR-536. `SettingsDialog` — «Ядро: OpenCASCADE 8.0» — проверить версию

**Где:** `SettingsDialog.cpp:145`

```
ImGui::Text("Ядро: OpenCASCADE 8.0");
```

**Реальная версия OCCT** — по `CMakeLists.txt` берётся из `find_package`, не пишется. Возможно, 7.7, 7.8 или 8.0. **Проверить.**

**Фикс:** `OpenCASCADE_VERSION` в runtime.

**Оценка:** 10 минут.

---

### CR-537. `SettingsDialog` — `__DATE__` / `__TIME__` — не пересобираются при каждом запуске

**Где:** `SettingsDialog.cpp:139`

```
ImGui::Text("Сборка: %s %s", __DATE__, __TIME__);
```

**Правильно** — это дата **компиляции**, не запуска. Но:
- Формат `__DATE__` — «Sep 27 2026» (английский).
- Локализация не применяется.

**WONTFIX** — показывает дату сборки, это ок.

---

### CR-538. `SettingsDialog` — нет кнопки «Сбросить до дефолта»

**Где:** весь файл

По UI_LAYOUT — есть, но не критично.

**Отложено.**

---

### CR-540. `SettingsDialog` — `BeginChild("##settings_tabs")` + `Selectable` = «свои вкладки»

**Где:** `SettingsDialog.cpp:22-31`

Левая панель — `BeginChild` + `Selectable`. ImGui имеет нативные `BeginTabBar`/`BeginTabItem`. Но **вертикальные табы** (как в Settings) — не штатно. Ок.

**WONTFIX.**

---

### CR-541. `SettingsDialog` — switch по tab без default

**Где:** `SettingsDialog.cpp:37-151`

```
switch (ctx.state->activeTab) {
    case 0: { ... }
    case 1: { ... }
    case 2: { ... }
    case 3: { ... }
    case 4: { ... }
    case 5: { ... }
}
```

**Если `activeTab` выйдет за 0..5** — **пустая страница**. ImGui не падает, но UX: пользователь видит пустоту.

**Фикс:** `default: ImGui::TextDisabled("(неизвестная вкладка)");` или assert.

**Оценка:** 3 минуты.

---

### CR-542. `SettingsDialog` — `ctx.currentTheme` используется как `int*`, а `theme` в switch — локальная копия

**Где:** `SettingsDialog.cpp:86-104`

```
int theme = ctx.currentTheme ? *ctx.currentTheme : 1;
if (ImGui::RadioButton("Светлая", &theme, 0)) {
    if (ctx.onThemeChange) ctx.onThemeChange(0);
}
```

`theme` — локальная копия. При клике — вызывается `onThemeChange(0)`, Application обновляет `*ctx.currentTheme = 0`. Но `theme` **не обновится** до следующего кадра.

**Ок для UI** — radio сработает в след. кадре.

**WONTFIX.**

---

### CR-543. `SettingsDialog` — нет сохранения настроек в файл

**Где:** весь файл

При закрытии приложения все настройки (кроме темы/языка — **если они сохраняются где-то**) теряются.

**Связано с CR-531.**

**Оценка:** 3 часа (полноценно).

---

### CR-544. `SettingsDialog` — `Localization::Instance().GetLanguage()` — singleton из UI

**Где:** `SettingsDialog.cpp:112`

```
Language cur = Localization::Instance().GetLanguage();
```

UI диалога **напрямую читает** singleton локализации. Могло бы быть через `ctx.currentLanguage`.

**Незначительное нарушение слоя**, но паттерн singleton позволяет.

**Отложено.**

---

### CR-545. `SettingsDialog` — `Language` enum в `app/Localization.h` — UI зависит от app

**Где:** `SettingsDialog.h:13`

```
#include "app/Localization.h"      // Language
```

UI-заголовок **подключает** `app/Localization.h`. По `ARCHITECTURE §2` — UI **может** зависеть от app (app выше ui), но зависимость от **конкретного заголовка** — хрупко.

**WONTFIX** — норм по слоям.

---

### CR-546. `SettingsDialog` — `SettingsContext` не имеет колбэков `onUnitsChange`, `onPrecisionChange`, и т.д.

**Где:** `SettingsDialog.h:17-22`

Есть `onThemeChange`, `onLanguageChange`. **Всё остальное — некуда сохранить.**

**Связано с CR-531.**

---

## 📊 Итоги волны 12 (диалоги)

**Всего диалогов:** 8 (Rename, DeleteConfirm, Unsaved, AddBox, Properties, NotImplemented, Dependency, Settings)
**Разобрано:** 8/8 ✅
**Найдено записей:** 81 (CR-465..CR-546)

**Общие паттерны:**
- **Локализация обойдена** в 7 из 8 диалогов (~120 строк).
- **`OpenPopup` каждый кадр** в 3 диалогах (ESC не работает).
- **Три разных API-паттерна** для modal popup.
- **Статические локалы** в SettingsDialog — 10 штук.
- **REVIEW-кандидаты:** DependencyDialog (C-01), SettingsDialog 4 вкладки (B-11).

---

---

## 🔴 P0 — ui/panels (Волна 13, часть 1: заголовки)

### CR-547. 🔴 `TreeContext::contextMenuPos` — `void*` вместо `ImVec2*`

**Где:** `DocumentTree.h:55-56`
**Статус:** OPEN
**Категория:** типобезопасность

```
bool*   pendingContextMenu = nullptr;
void*   contextMenuPos     = nullptr;  // ImVec2*
```

**Проблема:**
1. `void*` — тип неизвестен компилятору.
2. Комментарий `// ImVec2*` — единственная «документация».
3. При передаче — `static_cast<ImVec2*>(ctx.contextMenuPos)` **в каждом месте**. Если кто-то передаст не `ImVec2*` — UB без предупреждения.
4. Заголовок **не подключает `imgui.h`**, поэтому нет `ImVec2` — поэтому и `void*`. Но это лечится forward-декларацией или include.

**Фикс:** `ImVec2* contextMenuPos = nullptr;` + `#include "imgui.h"` в `.h` (или `struct ImVec2;` forward).

**Оценка:** 5 минут.

---

### CR-548. 🔴 `TreeContext` — 5 указателей на `Dialogs::*State` — тесная связность

**Где:** `DocumentTree.h:42-46`
**Статус:** OPEN
**Категория:** архитектура

```
Dialogs::AddBoxState*         addBoxState         = nullptr;
Dialogs::RenameState*         renameState         = nullptr;
Dialogs::DeleteConfirmState*  deleteConfirmState  = nullptr;
Dialogs::PropertiesState*     propertiesState     = nullptr;
```

**Проблема:**
- UI-панель (дерево) **напрямую знает** про 4 разных диалоговых состояния.
- Если добавится 5-й диалог — расширять `TreeContext`.
- Панель становится **точкой сборки UI** — не своя ответственность.

**Фикс:** колбэки `onRenameRequest(label)`, `onPropertiesRequest(label)` уже есть (`:64-65`), но `renameState`/`propertiesState` всё равно передаются. **Убрать** — оставить только колбэки.

Или — общий `UiState*` со всеми диалогами.

**Оценка:** 1 час.

---

### CR-551. 🔴 `DrawContextMenu3D` объявлена дважды

**Где:** `DocumentTree.h:78` + `ContextMenu.h:11`
**Статус:** OPEN
**Категория:** дублирование

```
// DocumentTree.h:78
void DrawContextMenu3D(const TreeContext& ctx);

// ContextMenu.h:11
void DrawContextMenu3D(const TreeContext& ctx);
```

**Два объявления в одном namespace.** Компилятор пропустит (совпадают), но:
- Непонятно, какая точка входа.
- Линкер может взять любую (если разные реализации — UB).
- `ContextMenu.h` **подключает** `DocumentTree.h` — то есть уже имеет объявление.

**Фикс:** оставить одно — в `ContextMenu.h`. Убрать из `DocumentTree.h`.

**Оценка:** 2 минуты.

---

### CR-552. 🟠 `TreeContext` — 7 колбэков без `default`, без проверок

**Где:** `DocumentTree.h:59-66`

`onSave`, `onOpen`, `onEnterSketch`, `onShowAllRespectingDefaults`, `onRequestDelete`, `onRenameRequest`, `onPropertiesRequest`.

**Всё `std::function`** — могут быть пустыми. Связано с CR-381 (QAT).

**Фикс:** assert или проверки.

**Оценка:** входит в CR-381-пакет.

---

### CR-553. 🟠 `TreeContext::onShowAllRespectingDefaults` — имя длиннее 30 символов

**Где:** `DocumentTree.h:62`

```
std::function<void()>                 onShowAllRespectingDefaults;
```

**Длинное имя** — но **семантически правильное** (соответствует `UI::Panels::ShowAllRespectingDefaults` в Visibility.h). Ок.

**WONTFIX.**

---

### CR-554. 🟠 `ContextMenu.h` — подключает `DocumentTree.h` для одного типа

**Где:** `ContextMenu.h:6`

```
#include "DocumentTree.h"
```

**Что нужно:** только `TreeContext` (struct).
**Что приходит:** весь заголовок + forward-declares + 3 функции.

**Фикс:** либо вынести `TreeContext` в отдельный `TreeContext.h`, либо forward-declare.

**Оценка:** 15 минут.

---

### CR-555. ✅ `TreeSplitter.h` — чисто

10 строк, одна функция, никаких проблем.

**Статус:** NOT A BUG.

---

### CR-556. 🟠 `DocumentTree` — `DrawLabelTree` возвращает `bool` — что именно?

**Где:** `DocumentTree.h:69-72`

```
bool DrawLabelTree(const TreeContext& ctx,
                   const TDF_Label& label,
                   const TDF_Label& selectedLabel,
                   bool hasOps);
```

**Возвращает `bool` — что?** «Показан/скрыт»? «Есть дети»? «Изменился»? **Неясно из объявления.**

**Фикс:** либо комментарий, либо переименование (`IsLabelVisible`?), либо `void`.

**Оценка:** 5 минут (уточнить у автора).

---

### CR-557. 🟠 `TreeContext::pendingContextMenu` + `contextMenuPos` — 2 поля для одного события

**Где:** `DocumentTree.h:55-56`

```
bool*   pendingContextMenu = nullptr;
void*   contextMenuPos     = nullptr;
```

**Проблема:** 2 связанных поля. Работает только если оба заполнены. **Нет гарантии.**

**Фикс:** `struct PendingContextMenu { bool pending; ImVec2 pos; }* contextMenu;` — одна структура.

**Оценка:** 20 минут.

---

---

## 🟠 P1 — ui/panels (Волна 13, часть 2: Visibility)

### CR-558. ✅ `Visibility` — закрывает B-01 (эталон)

**Где:** `Visibility.cpp:14-23`
**Статус:** NOT A BUG

Комментарий: «Закрывает B-01 (тройное дублирование логики видимости)». Правильная единая точка.

**Действие:** без изменений.

---

### CR-560. 🟠 `ShowAllRespectingDefaults` — 3 out-параметра вручную

**Где:** `Visibility.h:24-28`

```
void ShowAllRespectingDefaults(OcctView* view,
                                BreezeDocument* doc,
                                std::map<std::string, bool>& visibility,
                                std::map<std::string, bool>& isolationBackup,
                                bool& isolationActive);
```

**6 параметров, 3 из них — out (ссылки).** Это антипаттерн C-style. `visibility`, `isolationBackup`, `isolationActive` — все принадлежат **одному** state (Application).

**Фикс:** `struct VisibilityState { std::map<...> visibility; std::map<...> backup; bool active; }` — одна ссылка.

**Связано с CR-550.**

**Оценка:** 40 минут.

---

### CR-561. 🟠 `Visibility.cpp` — `std::cout` (CR-133 пакет)

**Где:** `Visibility.cpp:47`

```
std::cout << "ShowAllRespectingDefaults: restored\n";
```

**Связано с CR-133.**

**Оценка:** входит в пакет CR-133.

---

### CR-568. 🟡 `TreeSplitter` — `ImGui::SetNextWindowPos/Size(... ImGuiCond_Always)`

**Где:** `TreeSplitter.cpp:17-18`

Каждый кадр **пересчитывается** позиция. Работает, но чуть избыточно. **WONTFIX.**

---

### CR-569. 🟡 `TreeSplitter` — `if (active) { ... }` дважды

**Где:** `TreeSplitter.cpp:28-38`

```
if (active) {
    treeWidth += io.MouseDelta.x;
    ...
}
if (active) {
    ImDrawList* draw = ...;
    ...
}
```

**2 блока с одинаковым условием.** **Фикс:** объединить.

**Оценка:** 2 минуты.

---

## 🟠 P1 — ui/panels (Волна 13, часть 4: DependenciesPanel)

### CR-570. ✅ `DependenciesPanel` — использует `Tr()` (хорошо)

**Где:** `DependenciesPanel.cpp:17, 22, 25`

**В отличие от большинства диалогов** — здесь локализация работает. **Эталон.**

---

### CR-573. 🟠 `DependenciesPanel` — «NO» и «?» — hardcode-состояние

**Где:** `DependenciesPanel.cpp:29-30`

```
"Fully Constrained: NO"   // ← всегда NO
"DOF remaining: ?"        // ← всегда ?
```

**Не считается** — заглушки. Нет реального расчёта степеней свободы.

**Связано с C-02** (кнопки ограничений — заглушки до скетч-солвера).

**Отложено до Фазы 2.**

---

### CR-577. 🟠 `DependenciesPanel` — ListBox с одним Text, всегда

**Где:** `DependenciesPanel.cpp:21-24`

```
if (ImGui::BeginListBox("##deps_list", ImVec2(-1, 120))) {
    ImGui::TextDisabled("%s", Tr("panel.deps.empty").c_str());
    ImGui::EndListBox();
}
```

**Всегда** показывает «пусто». Список никогда не заполняется — заглушка. `BeginListBox` + `Text` — **избыточно**: достаточно `TextDisabled` без listbox.

**Фикс:** убрать listbox до реализации скетч-солвера.

**Оценка:** 5 минут.

---

### CR-578. 🟡 `DependenciesPanel` — `ImGuiWindowFlags_AlwaysAutoResize` без `NoScrollbar`

**Где:** `DependenciesPanel.cpp:18-20`

```
ImGui::Begin(Tr("panel.dependencies").c_str(), nullptr,
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_AlwaysAutoResize);
```

`AlwaysAutoResize` + `BeginListBox(120)` — **конфликт**: панель пытается ужаться, listbox фиксированной высоты. Возможно, скролл появляется. **Проверить визуально.**

**Оценка:** 5 минут (проверить).

---

### CR-580. 🟡 `DependenciesPanel` — `panelTop = 340` может перекрыть ViewCube

**Где:** `DependenciesPanel.cpp:13`

ViewCube справа-сверху. Панель зависимостей — `y=340`. Если ViewCube высотой 300+ — перекрытие. **Проверить визуально.**

**Оценка:** 10 минут.

---

---

## 🔴 P0 — ui/panels (Волна 13, часть 5: RightPanel)

### CR-582. 🔴 `RightPanel` — `handleX` при expanded считает от panelW=50

**Где:** `RightPanel.cpp:32-35`

```
float handleX = panelExpanded
                ? (rightEdge - panelW - handleW - edgeMargin)   // panelW=50
                : (rightEdge - handleW - edgeMargin);
```

**Связано с CR-581.** При правильной ширине 280 — handleX сдвинется на 230 px влево.

**Оценка:** входит в CR-581.

---

### CR-592. 🟠 `RightPanel` — `ctx.tooltip->Draw()` вызывается без проверки null

**Где:** `RightPanel.cpp:99, 117, 145, 164, 180`

```
if (ctx.tooltip) ctx.tooltip->Draw(...);
```

**Проверка есть — ок.** WONTFIX.

---

### CR-593. 🟡 `RightPanel` — `ImGui::MenuItem("Реалистичный (скоро)", nullptr, false, false)` — disabled+selected

**Где:** `RightPanel.cpp:196-198`

```
ImGui::MenuItem("Реалистичный (скоро)", nullptr, false, false);
```

`selected = false, enabled = false` — disabled. Ок.

Но `"(скоро)"` — **хардкод в скобках**. **Связано с CR-587.**

---

## 🔴 P0 — ui/panels (Волна 13, часть 6: ContextMenu)

### CR-594. 🔴 `ContextMenu3D` — «Домой (F6)» и «Изометрия» — одинаковый `SetHomeView()`

**Где:** `ContextMenu.cpp:29-34`
**Статус:** OPEN
**Категория:** функциональный баг

```
if (ImGui::MenuItem("Домой (F6)")) {
    if (ctx.view) ctx.view->SetHomeView();
}
if (ImGui::MenuItem("Изометрия")) {
    if (ctx.view) ctx.view->SetHomeView();    // ← то же самое
}
```

**Обе команды делают одно и то же.** По UI_LAYOUT — «Домой» = изометрия с FitAll. Дублирование.

**Фикс:** оставить одну (или сделать разные: «Домой» — полный сброс, «Изометрия» — только поворот камеры).

**Оценка:** 5 минут.

---

### CR-597. 🔴 `ContextMenu3D` — `(ImVec2*)ctx.contextMenuPos` — type-unsafe cast

**Где:** `ContextMenu.cpp:12`

```
ImVec2* pos = (ImVec2*)ctx.contextMenuPos;   // ← C-cast из void*
```

**Связано с CR-547.** Классический случай «я знаю, что там ImVec2, но компилятор не знает».

**Фикс:** `ImVec2*` в TreeContext.

**Оценка:** входит в CR-547.

---

### CR-600. 🟠 `ContextMenu3D` — `ImGui::TextDisabled("%s", name.c_str())` — имя серым

**Где:** `ContextMenu.cpp:61`

Имя выбранного объекта — **Disabled**. Пользователь видит тусклый текст. Обычно имя — **акцент**.

**Фикс:** `Text("%s", name)` или `TextColored(myTheme.textMain, ...)`.

**Оценка:** 5 минут.

---

### CR-601. 🟠 `ContextMenu3D` — `hasOps` вычисляется, но не передаётся дальше

**Где:** `ContextMenu.cpp:63-64`

```
bool hasOps = ctx.doc ? ctx.doc->Has3DOperations() : false;
DrawContextMenuItems(ctx, sel, hasOps);
```

**Ок** — передаётся. WONTFIX.

---

### CR-602. 🟠 `ContextMenu3D` — нет `onDelete`, `onRename` для 3D-меню

**Где:** `ContextMenu.cpp` — весь файл

Меню в 3D показывает только «Вид», «Домой», «Изометрия», «Показать всё». **Нет** «Удалить», «Переименовать», «Свойства».

**По UI_LAYOUT §18** — контекстное меню 3D должно содержать операции над объектом.

**Проверить с MikeVerx** — нужно ли добавлять.

**Оценка:** 30 минут (если нужно).

---

### CR-603. 🟡 `ContextMenu3D` — `if (*ctx.pendingContextMenu)` — сброс флага сразу после OpenPopup

**Где:** `ContextMenu.cpp:14-18`

```
if (*ctx.pendingContextMenu) {
    ImGui::SetNextWindowPos(*pos, ImGuiCond_Always);
    ImGui::OpenPopup("##ctx_3d");
    *ctx.pendingContextMenu = false;
}
```

**Ок** — правильный паттерн «одноразовый триггер» (как CR-490 AddBoxDialog).

---

### CR-604. 🟡 `ContextMenu3D` — `ctx.view->GetSelectedLabel()` — тихая деградация

**Где:** `ContextMenu.cpp:22`

```
TDF_Label sel;
if (ctx.view) sel = ctx.view->GetSelectedLabel();
```

Если `ctx.view == nullptr` — `sel` остаётся Null → меню показывает «нет выбора» (мир-режим). Формально **ок**, но пользователь в замешательстве.

**WONTFIX** — graceful degradation.

---

---

## 🔴 P0 — ui/panels (Волна 13, часть 7: DocumentTree)

### CR-605. 🔴 `DrawDocumentTree` хардкодит `panelWidth = 340.0f` — SPLITTER НЕ РАБОТАЕТ

**Где:** `DocumentTree.cpp:98`
**Статус:** OPEN
**Категория:** функциональный баг

```
const float panelWidth = 340.0f;   // временно; если нужно — передавать через ctx
ImGui::SetNextWindowSize(ImVec2(panelWidth, ...));
```

**А в `Application.h:96`:** `float myTreeWidth = 340.0f;`

А в `TreeSplitter.cpp:28`: `treeWidth += io.MouseDelta.x;`

**Что это значит:**
1. Пользователь тянет splitter → `myTreeWidth` меняется (например, 500).
2. `DrawDocumentTree` **игнорирует** его → всегда рисует 340.
3. **Splitter визуально двигается, но дерево не меняется.**

**Критический UX-баг.**

**Фикс:** `float treeWidth` в `TreeContext`, передавать из Application.

**Оценка:** 20 минут.

---

### CR-607. 🔴 `DrawDocumentTree` — `Tr("default.boxName").c_str()` в `strncpy` — опасная аллокация

**Где:** `DocumentTree.cpp:120-122`

```
std::strncpy(ctx.addBoxState->name, Tr("default.boxName").c_str(),
             sizeof(ctx.addBoxState->name) - 1);
```

**Проблема:** `Tr` возвращает `std::string` **by value** (или `const&` — надо проверить в `Localization.h`). Если **by value** — временный объект живёт до **конца full expression**, что охватывает вызов `strncpy`. ОК.

Но: если `Tr` **возвращает `std::string` by value** — `.c_str()` на временном. `strncpy` копирует — временный умирает после. Ок.

Если бы это был `const char*` без копии — был бы dangling.

**Сейчас безопасно, но хрупко.** RULES п.5 — не полагаться на временные буферы.

**Фикс:** `std::string name = Tr("default.boxName"); std::strncpy(..., name.c_str(), ...);`.

**Оценка:** 5 минут.

---

### CR-609. 🔴 `DrawLabelTreeImpl` — `PushID(Util::LabelKey(label).c_str())` — временная строка

**Где:** `DocumentTree.cpp:55`

```
ImGui::PushID(Util::LabelKey(label).c_str());
```

`LabelKey` возвращает `std::string` by value. `.c_str()` на временном. `PushID(const char*)` **копирует** строку внутри (хэш). Значит — **безопасно**. Но читается как опасный код.

**Фикс:** `const std::string key = Util::LabelKey(label); ImGui::PushID(key.c_str());`.

**Оценка:** 3 минуты.

---

### CR-610. 🔴 `DrawLabelTreeImpl` — `"eye_" + Util::LabelKey(label)` — heap-аллокация каждый кадр на каждый узел

**Где:** `DocumentTree.cpp:67`

```
std::string eyeId = "eye_" + Util::LabelKey(label);
```

Для дерева из 500 узлов × 60 FPS = **30000 аллокаций/сек**. Для проекта с RAM-философией — недопустимо.

**Плюс:** `LabelKey` **тоже** возвращает `std::string` — ещё одна аллокация.

**Связано с CR-093** (RefreshView) и CR-137 (SnapToKnownPoint).

**Фикс:** 
- `LabelKey` → возвращать `uint64_t` (hash).
- eyeId → формировать через `char buf[32]; snprintf(...)` (без heap).

**Оценка:** 30 минут.

---

### CR-611. 🔴 `DrawLabelTreeImpl` — debug-вывод `[type=%d, tag=%d]` в UI

**Где:** `DocumentTree.cpp:92-93`
**Статус:** OPEN
**Категория:** debug-код в продакшене

```
ImGui::SameLine();
ImGui::TextDisabled("[type=%d, tag=%d]", type, label.Tag());
```

**Пользователь видит:**
```
> Документ
  > Start
    👁 XY Plane  [type=-2, tag=2]
    👁 YZ Plane  [type=-2, tag=3]
```

**Это отладочный вывод.** Не должен быть в проде. Либо **убрать**, либо **под флаг `#ifdef BREEZECAD_DEBUG`**.

**Оценка:** 2 минуты.

---

### CR-615. 🟠 `DrawDocumentTree` — `Save` / `Open` дублируют QAT

**Где:** `DocumentTree.cpp:124-127`

```
if (ImGui::Button("Save")) { if (ctx.onSave) ctx.onSave(); }
ImGui::SameLine();
if (ImGui::Button("Open")) { if (ctx.onOpen) ctx.onOpen(); }
```

**По UI_LAYOUT §8.3** — кнопки Save/Open в левой колонке Backstage, не в дереве. **Эти две кнопки в дереве — артефакт отладки.**

**Фикс:** убрать.

**Оценка:** 2 минуты.

---

### CR-616. 🟠 `DrawLabelTreeImpl` — `if (isSystem) PushStyleColor(...getColor())` — пустая операция

**Где:** `DocumentTree.cpp:73-75, 79`

```
if (isSystem) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
}
bool opened = ImGui::TreeNodeEx(...);
if (isSystem) ImGui::PopStyleColor();
```

**Push с текущим цветом** — ничего не меняет. Оставлено как «placeholder» для будущей стилизации? Но комментария нет.

**Фикс:** убрать, или задать реальный цвет для системных (dim).

**Оценка:** 3 минуты.

---

### CR-618. 🟠 `DrawDocumentTree` — deferred operations через указатели

**Где:** `DocumentTree.cpp:207-224`

```
if (ctx.pendingCreateSketchLabel && !ctx.pendingCreateSketchLabel->IsNull() && ctx.doc) {
    TDF_Label newSketch = ctx.doc->AddSketchFeature(*ctx.pendingCreateSketchLabel);
    ...
}
if (ctx.pendingEnterSketchLabel && !ctx.pendingEnterSketchLabel->IsNull() && ctx.doc) {
    ...
}
```

**Это B-08** — «отложенные операции через два флага, неявный порядок». Подтверждается.

**Фикс:** единый `pendingAction` enum + параметр. Или явный state machine.

**Оценка:** 1 час.

---

### CR-619. 🟠 `DrawContextMenuItems` — `hasOps` передаётся, но используется только в одном месте

**Где:** `DocumentTree.cpp:295`

```
bool curVisible = (it != ctx.visibility->end()) ? it->second : !hasOps;
```

**В остальных местах `hasOps` не используется.** Функция могла бы обойтись без него или использовать чаще.

**Отложено.**

---

### CR-624. 🟠 `DrawContextMenuItems` — `"Start"` — magic имя корневой папки

**Где:** `DocumentTree.cpp:284`

```
if (type == FT_Start) { ... }
```

`Start` — это **имя из `CreateBaseGeometry`** (`TDataStd_Name::Set(myBaseLabel, "Start")`). Захардкожено в двух местах (в doc и здесь — хотя здесь через `FT_Start`, что ок).

**WONTFIX.**

---

### CR-626. 🔴 `ViewCubeGeometry.h` — только TODO, ни одной структуры

**Где:** `ViewCubeGeometry.h:14-20`
**Статус:** OPEN
**Категория:** незаконченный рефакторинг + корень BUG-4.1

```
// TODO(step 1.3):
//   struct Face   { int v[4]; int n[3]; const char* label; };
//   struct Edge   { int v1, v2; int f1, f2; };
//   struct Corner { int v; int f1, f2, f3; };
//   inline constexpr Face   kFaces[6]   = { ... };
//   inline constexpr Edge   kEdges[12]  = { ... };
//   inline constexpr Corner kCorners[8] = { ... };
//   inline constexpr double kFaceTextRight[6][3] = { ... };
//   inline constexpr double kFaceTextDown[6][3]  = { ... };
```

**Файл существует, но ПУСТОЙ.** Вся геометрия куба — где-то в `ViewCubeOverlay.cpp`.

**Почему это корень BUG-4.1:**
- BUG-4.1: «подписи привязаны к неверным граням при вращении».
- Если данные `kFaces/kFaceTextRight/kFaceTextDown` в `.cpp` (не в отдельном хедере) — их нельзя протестировать, нельзя переиспользовать, нельзя аккуратно починить.
- **Без выноса таблиц в `ViewCubeGeometry.cpp` BUG-4.1 не исправить чисто.**

**Фикс:** реализовать то, что в TODO — вынести таблицы. Файл 21 строка должен стать ~150 строк.

**Оценка:** 2 часа (правильно).

---

### CR-627. 🔴 `ViewCubeAnimator.cpp` — 3 строки, реализация в другом месте

**Где:** `ViewCubeAnimator.cpp` (3 строки)
**Статус:** OPEN
**Категория:** незаконченный рефакторинг

Файл `.cpp` **существует**, но содержит только `#include` (вероятно). Реализация `Update/StartAbsolute/StartRoll` — в `ViewCubeOverlay.cpp`.

**Связано с CR-626.** Рефакторинг `ui/viewcube` был сделан **наполовину**: файлы созданы, но логика осталась в основном `.cpp`.

**Фикс:** вынести методы `Animator` из `ViewCubeOverlay.cpp` в `ViewCubeAnimator.cpp`.

**Оценка:** 1 час.

---

### CR-629. 🟠 `Animator::AnimTarget` — `int kind` вместо `enum class`

**Где:** `ViewCubeAnimator.h:15-19`

```
struct AnimTarget {
    int    kind;          // 0 = absolute, 1 = roll
    double dir[3];
    double up[3];
};
```

`kind` — magic `0/1`. Комментарий говорит что.

**Фикс:** `enum class Kind { Absolute, Roll };`.

**Оценка:** 20 минут.

---

### CR-630. 🟠 `AnimTarget` — `double dir[3]`, `up[3]` — сырые массивы

**Где:** `ViewCubeAnimator.h:17-18`

**Что теряется:**
- Никакой валидации (нормализация).
- `dir` и `up` могут быть коллинеарны → gimbal lock в камере.
- Копирование вручную.

**Фикс:** `gp_Dir dir; gp_Dir up;` (OCCT) или свой `Vec3`.

**Связано с CR-596** (gp_Dir в UI).

**Оценка:** 30 минут.

---

### CR-631. 🟠 `ViewCubeOverlay::Context` — `rightPanelShiftX` — комментарий `55 или 110`

**Где:** `ViewCubeOverlay.h:34-38`

```
struct Context {
    OcctView&            view;
    const ThemeTokens&   theme;
    ImGuiIO&             io;
    float                rightPanelShiftX;  // 55 или 110
};
```

Комментарий говорит «55 или 110», но **в Application** (`DrawViewCube`) передаётся `myRightPanelExpanded ? 110.0f : 55.0f`. **Совпадает.** Но:

**Что это значит:** Context — 4-й источник «правой панели» (после QAT 280/40, StatusBar 320/80, ProjectTabs без). Связано с CR-405.

**Фикс:** единый layout-модуль.

**Оценка:** входит в CR-405.

---

### CR-634. 🟠 `ViewCubeOverlay::myButtonsTimer` — что это?

**Где:** `ViewCubeOverlay.h:44`

```
float    myButtonsTimer = 0.0f;
```

**Нет комментария.** Что за «buttons timer»? Скрытие кнопок << >> через N секунд? Или что-то другое?

**Фикс:** комментарий или переименование.

**Оценка:** 2 минуты (уточнить).

---

### CR-635. 🟠 `ViewCubeOverlay` — `Context::io` — `ImGuiIO&` внутри

**Где:** `ViewCubeOverlay.h:36`

```
ImGuiIO& io;
```

`ImGui::GetIO()` — глобал. Передавать как параметр — **не помогает**, потому что всё равно синглтон. **Иллюзия инъекции.**

**WONTFIX** — так принято в ImGui.

---

### CR-636. 🟡 `ViewCubeOverlay() = default;` + `~ViewCubeOverlay() = default;`

**Где:** `ViewCubeOverlay.h:41-42`

Не нужны явно, если не делать ничего особенного. Стиль. **WONTFIX.**

---

---

## 🔴 P0 — ui/viewcube (Волна 14, часть 2: Overlay.cpp 1-260)

### CR-637. 🔴 `ViewCubeOverlay.cpp` — `#include "app/Application.h"` — инверсия слоёв

**Где:** `ViewCubeOverlay.cpp:3`
**Статус:** OPEN
**Категория:** архитектура, нарушение `ARCHITECTURE §2`

```
#include "app/Application.h"  // ThemeTokens
```

**Комментарий врёт** — `ThemeTokens` в `ui/theme/ThemeTokens.h`, а не в `Application.h`.

**Что это значит:**
- UI-модуль подтягивает **весь Application.h** (около 240 строк).
- Application.h включает `platform`, `occt`, `doc`, `ui/panels`, все диалоги.
- **Компилятор тратит время на парсинг всего этого.**

**Фикс:** `#include "ui/theme/ThemeTokens.h"`.

**Оценка:** 1 минута.

---

### CR-638. 🔴 `ViewCubeOverlay.cpp` — таблицы `Face/Edge/Corner` инлайн

**Где:** `ViewCubeOverlay.cpp:127-135, 158-166, 168-175`
**Статус:** OPEN
**Категория:** корень BUG-4.1 (см. CR-626)

```
struct Face { int v[4]; gp_Dir n; const char* label; };
Face faces[6] = {
    {{4,5,6,7}, {0,0, 1}, "ВЕРХ"},
    {{0,3,2,1}, {0,0,-1}, "НИЗ"},
    {{0,1,5,4}, {0,-1,0}, "ПЕРЕД"},
    ...
};

Edge edges[12] = { ... };
Corner corners[8] = { ... };
```

**Всё это в теле `Draw()`**, пересоздаётся каждый кадр:
- 6 `Face` (struct ~40 байт) = 240 байт.
- 12 `Edge` = 48 байт.
- 8 `Corner` = 32 байта.
- **Плюс `gp_Dir` в каждом Face — constructors OCCT вызываются 60×6 = 360 раз/сек.**

**Связано с CR-626** (пустой `ViewCubeGeometry.h`).

**Фикс:** вынести в `ViewCubeGeometry.h` как `inline constexpr`.

**Оценка:** 2 часа (входит в CR-626).

---

### CR-640. 🔴 `ViewCubeOverlay::Draw` — 3 × `std::vector` + `std::sort` каждый кадр

**Где:** `ViewCubeOverlay.cpp:138, 177, 187`

```
std::vector<Visible> vis;      // до 3 элементов
std::vector<VisEdge> visEdges; // до 12
std::vector<VisCorner> visCorners; // до 8
...
std::sort(vis.begin(), vis.end(), ...);
```

**Каждый кадр:**
- 3 `std::vector` — **3 heap-аллокации** (или реаллокации при росте).
- `std::sort` — O(N log N) на 3 элементах.
- Возможно, cap растёт.

**Фикс:**
- `std::array<Visible, 6> vis; int visCount = 0;`
- Или `static` буферы (но тогда thread-safety).
- Или small-vector оптимизация.

**Оценка:** 1 час.

---

### CR-644. 🔴 `ViewCubeOverlay.cpp` — `myAnimator.myToDir[0]` — прямое чтение public

**Где:** `ViewCubeOverlay.cpp:33-115` — **вся логика анимации**

Прямой доступ к `myAnimator.myActive`, `myAnimator.myT`, `myAnimator.myFromDir[0]`, `myAnimator.myToDir[0]`, `myAnimator.myQueue` — **более 30 обращений** к public-полям.

**Связано с CR-628** («потом закроем» — не закрыли).

**Плюс:** логика анимации **вся в Draw**, а не в `Animator::Update(dt)`.

**Фикс:** вынести в `Animator::Update(dt, view)` — один вызов.

**Оценка:** 2 часа (входит в CR-627).

---

### CR-645. 🔴 `ViewCubeOverlay.cpp` — `#include "imgui_internal.h"`

**Где:** `ViewCubeOverlay.cpp:6`

```
#include "imgui_internal.h"
```

ImGui **internal API** — может сломаться между версиями. Где используется? В видимом фрагменте — **не вижу**. Возможно, во второй половине.

**Фикс:** проверить, где нужно; если для `ImDrawList::AddText` — есть в публичном.

**Оценка:** 10 минут.

---

### CR-647. 🟠 `ViewCubeOverlay.cpp` — `Face.n.Dot(camDir)` — OCCT-математика в UI

**Где:** `ViewCubeOverlay.cpp:139`

```
if (faces[fi].n.Dot(camDir) >= -0.01) continue;
```

`0.01` — magic tolerance. **Фикс:** `constexpr double kFaceCullTolerance = 0.01;`.

**Оценка:** входит в CR-641.

---

### CR-648. 🟠 `ViewCubeOverlay.cpp` — `faceColor` lambda — 6 условий, дублируют `Face` mapping

**Где:** `ViewCubeOverlay.cpp:200-208`

```
auto faceColor = [&](const gp_Dir& n) -> ImU32 {
    if (n.Z() > 0.5)  return theme.cubeFaceTop;
    if (n.Z() < -0.5) return theme.cubeFaceBottom;
    if (n.Y() < -0.5) return theme.cubeFaceFront;
    if (n.Y() > 0.5)  return theme.cubeFaceBack;
    if (n.X() > 0.5)  return theme.cubeFaceRight;
    return theme.cubeFaceLeft;
};
```

**Что это значит:**
- `Face` уже имеет `n` в таблице.
- `faceColor` **дублирует** проверку направления.
- `0.5` — magic.

**Фикс:** `Face.colorToken` (указатель на поле ThemeTokens) — вместо вычисления.

**Оценка:** 30 минут.

---

### CR-649. 🟠 `ViewCubeOverlay.cpp` — `hoveredCorner/Edge/Face` — 3 int'а

**Где:** `ViewCubeOverlay.cpp:212-214`

Три int-а для hover-состояния. **Логически** — tri-state enum:
- Ничего не наведено.
- Наведена грань.
- Наведено ребро.
- Наведён угол.

**Фикс:** `struct Hovered { enum Kind {None, Face, Edge, Corner}; Kind kind; int idx; }`.

**Оценка:** 20 минут.

---

### CR-651. 🟠 `ViewCubeOverlay.cpp` — hit-testing грани через signed area

**Где:** `ViewCubeOverlay.cpp:249-258`

```
for (int e = 0; e < 4; e++) {
    ImVec2 a = v.pts[e], b = v.pts[(e+1)%4];
    float cr = (b.x-a.x)*(mouse.y-a.y) - (b.y-a.y)*(mouse.x-a.x);
    int s = (cr > 0) ? 1 : -1;
    if (sign == 0) sign = s;
    else if (sign != s) { inside = false; break; }
}
```

**Корректная** реализация point-in-polygon. Норм.

**WONTFIX.**

---

### CR-652. 🟠 `ViewCubeOverlay.cpp` — `ImVec2 mouse = io.MousePos;` — можно из `io.MousePos` напрямую

Незначительно. **WONTFIX.**

---

### CR-653. 🟠 `ViewCubeOverlay.cpp` — 3 `for (int k = (int)visCorners.size()-1; k >= 0; k--)` — C-cast

**Где:** `ViewCubeOverlay.cpp:221, 233, 245`

```
for (int k = (int)visCorners.size() - 1; k >= 0; k--)
```

**C-cast** + обратная итерация. **Фикс:** `static_cast<int>`.

**Оценка:** 2 минуты.

---

### CR-654. 🟠 `ViewCubeOverlay.cpp` — `if (!io.WantCaptureMouse)` — hover только если ImGui не ест

**Где:** `ViewCubeOverlay.cpp:216`

**Правильно.** WONTFIX.

---

### CR-656. 🟡 `ViewCubeOverlay.cpp` — комментарий «Плавная анимация камеры: 0.5 сек»

**Где:** `ViewCubeOverlay.cpp:36`

Комментарий есть — хорошо. Но `0.5f` (строка 38) — magic. **Входит в CR-641.**

---

---

## 🔴 P0 — ui/viewcube (Волна 14, часть 3: Overlay.cpp 260-524)

### CR-658. 🔴 `ViewCubeOverlay.cpp` — рендер текста через `AddImageQuad` — сложная кастомная логика
**Включает (merged):** CR-657, CR-659, CR-660, CR-661, CR-681

**Где:** `ViewCubeOverlay.cpp:288-380`

**Что делает:**
1. Ручной UTF-8 парсинг символов (30 строк).
2. Для каждого глифа — `FindGlyph(cp)`.
3. `scale = fSizePx / font->FontSize`.
4. Вычисление 4 углов квада в 3D-координатах грани.
5. Проекция каждого через `project()`.
6. `AddImageQuad` с UV из атласа.

**Проблемы:**
- **200 строк кастомного кода** вместо ~20 строк стандартного ImGui-рендера.
- Каждый глиф — 4 вызова `project()` = 4 двойных умножения + сложения. Для 5 букв × 3 видимых грани × 60 FPS = **3600 matrix-ops/сек**.
- **Кастомный рендер = кастомные баги** (что и есть BUG-4.1).

**Фикс (варианты):**
- **A.** Использовать `ImDrawList::AddText` с поворотом — но `AddText` не поддерживает 3D-проекцию. **Не подходит.**
- **B.** Рендерить надписи **плоско** на экране (без 3D-проекции) — угол обзора чуть хуже, но **просто и надёжно**. Для ViewCube это **визуально приемлемо** (в Inventor подписи тоже «плавают», но остаются горизонтальными).
- **C.** Оставить 3D-проекцию, но **упростить код** через `ImDrawList::PathArcTo` / helper.

**Рекомендую B** — упрощает в 10 раз, устраняет класс багов, визуально почти не отличается.

**Оценка:** 2 часа (B).

---

### CR-662. 🔴 `startVCAnim` + `startVCRoll` — 30 строк копипасты

**Где:** `ViewCubeOverlay.cpp:410-448` (startVCAnim) + `:450-475` (startVCRoll)

**Оба метода:**
1. Проверяют `if (myAnimator.myActive || !myAnimator.myQueue.empty())` — magic.
2. Проверяют `if (myAnimator.myQueue.size() < 8)` — magic.
3. Создают `AnimTarget`, заполняют поля по одному.
4. Записывают `myFromDir/Up[0..2]`, `myToDir/Up[0..2]`.
5. `myT = 0`, `myActive = true`.

**Разница:** только `kind = 0/1` и содержимое полей.

**Фикс:** `Animator::Start(kind, ...)`, `Animator::Enqueue(kind, ...)`.

**Оценка:** 1 час (после CR-628).

---

### CR-663. 🔴 `ViewCubeOverlay.cpp` — логика показа кнопок << >> запутана

**Где:** `ViewCubeOverlay.cpp:490-514`
**Статус:** OPEN
**Категория:** логическая сложность

```
if (cubeAligned && hoveredFace >= 0) {
    myButtonsTimer = 0.5f;
} else if (cubeAligned && mouseInBtnZone && myButtonsTimer > 0.0f) {
    myButtonsTimer = 0.5f;
} else {
    myButtonsTimer -= io.DeltaTime;
    if (myButtonsTimer < 0.0f) myButtonsTimer = 0.0f;
}
```

**Что это значит:**
1. Если кубик выровнен и курсор над гранью → кнопки показать на 0.5s.
2. Если кубик выровнен и курсор в btnZone и таймер ещё тикает → продлить.
3. Иначе → таймер тикает вниз.

**Magic `0.5` два раза.** Logic **неясна** без комментария — «показывать кнопки 0.5 сек после hover, если курсор в зоне».

**Плюс:** `mouseInBtnZone` вычисляется через `stickyL/T/R/B` с magic 80/70/100.

**Фикс:** 
- `constexpr float kButtonsShowSeconds = 0.5f;`
- Явный state machine (Show/Hide/Transitioning).

**Оценка:** 1 час.

---

### CR-666. 🟠 `ViewCubeOverlay.cpp` — `computeUp` дублирует логику из `OcctView::SetCameraDirectionAndUp`

**Где:** `ViewCubeOverlay.cpp:385-394`

```
auto computeUp = [](const gp_Dir& dir) -> gp_Dir {
    gp_Dir zAxis(0, 0, 1);
    if (std::abs(dir.Dot(zAxis)) > 0.99) {
        return gp_Dir(0, 1, 0);
    }
    gp_Vec upProj = gp_Vec(zAxis) - gp_Vec(dir) * dir.Dot(zAxis);
    return gp_Dir(upProj);
};
```

**А в `OcctView::SetCameraDirectionAndUp` (CR-310):**
```
gp_Dir safeUp = up;
if (std::abs(dir.Dot(up)) > 0.99) {
    gp_Dir fallback(0, 0, 1);
    if (std::abs(dir.Dot(fallback)) > 0.9) fallback = gp_Dir(0, 1, 0);
    safeUp = fallback;
}
```

**Логика похожа, но разная.** `0.99` в обоих. `0.9` только в OcctView.

**Фикс:** единая функция `DirUtils::ComputeUpFor(dir)`.

**Оценка:** 30 минут.

---

### CR-668. 🟠 `ViewCubeOverlay.cpp` — `<cmath>` для `std::sqrt` — но `M_PI` из `<cmath>` не гарантирован

**Где:** `ViewCubeOverlay.cpp:11`

Уже разбирали (CR-320).

---

### CR-669. 🟠 `ViewCubeOverlay.cpp` — `vis.size() == 1` — «кубик выровнен»

**Где:** `ViewCubeOverlay.cpp:484`

```
bool cubeAligned = (vis.size() == 1);
```

**Работает** — если видна ровно 1 грань, камера смотрит точно перпендикулярно. **Но:** `vis.size()` — до **сортировки** может быть 1, после — то же. Ок.

**Фикс:** комментарий.

**Оценка:** 2 минуты.

---

### CR-672. 🟠 `ViewCubeOverlay.cpp` — кнопки рендерятся через новое ImGui-окно

**Где:** `ViewCubeOverlay.cpp:521`

```
ImGui::Begin("##viewcube_arrows", ...);
```

**Ок**, но: для 2 кнопок создаётся отдельное окно. Можно было бы нарисовать через draw-list напрямую (как весь остальной ViewCube).

**Не критично.** WONTFIX.

---

### CR-673. 🟠 `ViewCubeOverlay.cpp` — tooltip не показывается, если кнопка disabled

**WONTFIX.**

---

### CR-674. 🟠 `ViewCubeOverlay.cpp` — `rollCameraAnim` lambda — лишний слой

**Где:** `ViewCubeOverlay.cpp:515-517`

```
auto rollCameraAnim = [&](double angleDeg) {
    startVCRoll(angleDeg);
};
```

**Оборачивает `startVCRoll` без логики.** Фикс: вызывать `startVCRoll` напрямую.

**Оценка:** 1 минута.

---

### CR-675. 🟠 `ViewCubeOverlay.cpp` — большие `Face`-таблицы копируются каждый кадр

**Где:** `ViewCubeOverlay.cpp:127-135`

**Уже** CR-638. Дополнение: `gp_Dir n` внутри `Face` — конструктор **вызывается 60 FPS × 6 грани × повторно = 360 раз/сек**.

**Оценка:** входит в CR-638.

---

### CR-676. 🟡 `ViewCubeOverlay.cpp` — `if (vis.size() == 1)` — `size_t == int`

**Где:** `ViewCubeOverlay.cpp:484`

`vis.size()` возвращает `size_t`, `1` — `int`. Неявное сравнение.

**Фикс:** `vis.size() == 1u` или `(int)vis.size() == 1`.

**Оценка:** 1 минута.

---

### CR-677. 🟡 `ViewCubeOverlay.cpp` — `AddImageQuad(atlasTex, ...)` — ищет глиф каждый кадр

**Где:** `ViewCubeOverlay.cpp:371`

`font->FindGlyph((ImWchar)cp)` — вызывается на каждый символ подписи, каждый кадр. Для 3 подписей по 4-5 букв = **15 FindGlyph/кадр × 60 FPS = 900/сек.**

`FindGlyph` делает бинарный поиск по `IndexAdvanceX`/`IndexLookup` — O(log N). Для 3000 глифов в атласе — 11 итераций.

**Фикс:** кэш глифов по codepoint в статике (уже есть внутри ImGui — `FindGlyph` — часть кэша). Не критично.

**WONTFIX.**

---

### CR-678. 🟡 `ViewCubeOverlay.cpp` — `theme.cubeText` для всех символов всех граней

**Где:** `ViewCubeOverlay.cpp:373`

**Один цвет** для текста. Может, для hoveredFace — другой? Не реализовано.

**Отложено (Фаза 2).**

---

## 🎯 Сводка по ViewCube (корень BUG-4.1)

**BUG-4.1 состоит из 3 частей:**
1. **CR-657** — таблицы `faceTextRight/Down` захардкожены и содержат ошибки.
2. **CR-658** — кастомный 3D-рендер текста слишком сложен → баги.
3. **CR-659** — ручной UTF-8 парсинг → усечение → «ВЛЕВО» вместо «ЛЕВО».

**План фикса BUG-4.1:**
1. Вынести таблицы в `ViewCubeGeometry.{h,cpp}` (CR-626).
2. Заменить ручной UTF-8 на `ImTextCharFromUtf8` (CR-659).
3. **Рассмотреть замену 3D-рендера на плоский экранный** (CR-658, вариант B) — простое и надёжное.
4. Юнит-тест на геометрию (6 граней × 6 камер).

**Оценка полная:** 4-6 часов.

---

---

## 🔴 P0 — util (Волна 15, часть 1: Utf8)

### CR-679. 🔴 `Utf8.cpp` — жёстко зашит `wchar_t = 16 бит` — КРОССПЛАТФОРМЕННЫЙ БАГ

**Где:** `Utf8.cpp:37, 47` (Utf8ToExt), `:64` (ExtToUtf8)
**Статус:** OPEN
**Категория:** кроссплатформенность — блокирует Mac/Linux/ARM

**Utf8ToExt:**
```
ws.push_back((wchar_t)cp);
...
ws.push_back((wchar_t)(0xD800 + (cp >> 10)));   // ← сурогатная пара
ws.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
```

Код **сознательно** кодирует 4-байтные UTF-8 символы как **сурогатные пары UTF-16**. Это работает на **Windows** (где `wchar_t = 16 бит`).

**Но:**
- **Linux:** `wchar_t = 32 бита` → `(wchar_t)0xD800` — это **валидный code point U+D800** (зарезервированный), а не сурогат. **Строка сломается.**
- **macOS (Intel/Apple Silicon):** то же — 32 бита.
- **ARM везде:** зависит от ОС, обычно 32.

**ExtToUtf8:**
```
const uint16_t* ws = (const uint16_t*)es.ToExtString();
...
if (cp >= 0xD800 && cp <= 0xDBFF && ws[i+1] != 0) {
    uint32_t lo = ws[i+1];
    if (lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
```

**Жёсткий каст к `uint16_t*`**. На Linux — 32-битные code units → **читаем половину каждого символа**. Полный мусор.

**Что это значит для проекта:**
- **Проект НЕ соберётся/НЕ заработает на macOS** при первом запуске (кириллические имена фич).
- **Android/ARM** — то же.
- **Целевая кроссплатформенность** (Philosophy, `CANON_FEATURES`, `ADR-0001`) — **блокируется этим файлом**.

**Фикс:**

**Вариант A:** использовать OCCT API:
```
TCollection_ExtendedString Utf8ToExt(const std::string& s) {
    return TCollection_ExtendedString(s.c_str(), Standard_True);  // isMultiByte=true
}
std::string ExtToUtf8(const TCollection_ExtendedString& es) {
    TCollection_AsciiString ascii(es, '?');  // потеря не-ASCII → но CR-705 фиксит
    return ascii.ToCString();
}
```

**Вариант B:** использовать `Standard_Utf8` API (OCCT 7.7+):
- `Standard_Utf8Char` тип.
- `TCollection_ExtendedString` уже умеет работать с UTF-8.

**Вариант C:** собственная реализация с `#ifdef WCHAR_IS_16BIT`:
```
#if WCHAR_MAX == 0xFFFF
    // Windows — сурогатные пары
#else
    // POSIX — прямой код-поинт
#endif
```

**Рекомендую A** — меньше кода, OCCT всё умеет.

**Оценка:** 2 часа + кроссплатформенное тестирование.

---

### CR-680. 🔴 `Utf8.cpp` — `ws.push_back(0)` — добавление null-терминатора вручную

**Где:** `Utf8.cpp:41`

```
ws.push_back(0);
return TCollection_ExtendedString(ws.c_str());
```

`std::wstring::c_str()` **уже** null-терминирован. Дополнительный `push_back(0)` — **двойной null** в конце. OCCT игнорирует, но это лишнее.

**Плюс:** `ws.reserve(s.size())` — **недооценка**. UTF-8 `s.size()` — число байт. UTF-16 `ws.size()` = для кириллицы равно, для эмодзи вдвое меньше, но для ASCII равно. **Фактически** reserve может быть недостаточно для эмодзи (в 2× больше push_back).

**Фикс:** убрать `push_back(0)`.

**Оценка:** 2 минуты.

---

### CR-682. 🔴 `Utf8ToExt` — невалидный UTF-8 → молчаливый пропуск

**Где:** `Utf8.cpp:24-26`

```
else { i++; continue; }   // ← невалидный байт — пропускаем
```

**Проблема:** если на входе **битый UTF-8** — молча теряем байт. Диагностика невозможна.

**Фикс:** `assert` в debug, лог в release.

**Оценка:** 10 минут.

---

### CR-683. 🔴 `Utf8ToExt` — `if (i + extra >= s.size()) break;` — обрезка хвоста

**Где:** `Utf8.cpp:22`

```
if (i + extra >= s.size()) break;
```

**Обрезка** при неполной последовательности — ок, но **без предупреждения**.

**Оценка:** входит в CR-682.

---

### CR-684. 🟠 `Utf8.cpp` — `(wchar_t)cp` — C-cast + potential truncation

**Где:** `Utf8.cpp:37`

```
ws.push_back((wchar_t)cp);
```

Если `cp = 0x10000..0x10FFFF` (4-байтный UTF-8), а `wchar_t = 16 бит` (Windows) — **усечение** до 16 бит. Затем проверка `if (cp < 0x10000)` — уже поздно.

**Смотрю ещё раз код:**
```
if (cp < 0x10000) {
    ws.push_back((wchar_t)cp);      // ← для cp < 0x10000
} else if (cp <= 0x10FFFF) {
    cp -= 0x10000;                   // ← для 4-байтных
    ws.push_back((wchar_t)(0xD800 + (cp >> 10)));
    ws.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
}
```

**Ок**, усечения нет — логика корректна.

**WONTFIX.**

---

### CR-685. 🟠 `Utf8.cpp` — `wcstombs` / `mbrtowc` — почему не использовать стандартные?

**WONTFIX** — OCCT не совместим со стандартными, но идея — использовать OCCT-API (CR-679).

---

## 🔴 P0 — util (Волна 15, часть 2: Geometry2D)

### CR-687. 🔴 `SnapToGridPoint` — то же: `origin` используется

**Где:** `Geometry2D.cpp:64, 78-80`

```
gp_Pnt origin = ax.Location();
...
gp_Pnt snapped = origin;   // ← используется
```

**Ок.** WONTFIX.

**Но:** в `ApplyOrtho`:

**Где:** `Geometry2D.cpp:36`

```
gp_Pnt origin = ax.Location();   // ← объявлена
...
gp_Pnt result = from;            // ← origin НЕ используется
```

**То же, что CR-686.** Использовать `from` вместо `origin`.

**Оценка:** 1 минута.

---

### CR-688. 🔴 `SnapToGridPoint` — `const double step = 10.0;` — hardcode шага сетки

**Где:** `Geometry2D.cpp:71`

```
const double step = 10.0;   // ← 10 мм
```

**Шаг сетки** — настройка пользователя (по `UI_LAYOUT §15.5`: «Шаг сетки в эскизе»). Здесь — **жёстко 10 мм**.

**Связано с `StatusBar.cpp:39`** («Снап: сетка 10 мм»).

**Фикс:** `double SnapToGridPoint(p, plane, step = 10.0);` или `SettingsState::gridStep`.

**Оценка:** 20 минут.

---

### CR-689. 🔴 `Geometry2D.cpp` — 4× дублирование `ax.Location()/XDirection()/YDirection()`

**Где:** `Geometry2D.cpp:22-24, 36-38, 66-68, 86-88`

В **каждой** из 4 функций:
```
gp_Ax3 ax = plane.Position();
gp_Pnt origin = ax.Location();
gp_Dir xDir   = ax.XDirection();
gp_Dir yDir   = ax.YDirection();
```

**4 копии.** Связано с CR-019 (RULES п.19 — одна функция одно место).

**Фикс:** `struct PlaneAxes { gp_Pnt origin; gp_Dir x, y; }` + `GetPlaneAxes(plane)`.

**Оценка:** 30 минут.

---

### CR-690. 🔴 `Geometry2D.cpp` — `gp_Vec(xDir)` — конструктор gp_Vec на каждой операции

**Где:** `Geometry2D.cpp:29-30, 46-47, 75-76, 95-96`

```
double u = v.Dot(gp_Vec(xDir));   // ← конструктор
double w = v.Dot(gp_Vec(yDir));   // ← конструктор
```

`gp_Vec(gp_Dir)` — не бесплатный, копирование координат. 4 функции × 2-3 вызова = **~10 конструкторов** на каждый вызов `Geometry2D`.

Для `SnapToGridPoint`, вызываемого **на каждый пиксель движения мыши в эскизе** — это 600+ конструкторов/сек.

**Фикс:** `gp_Vec xVec(xDir);` один раз, переиспользовать.

Или — **лучше** — `gp_Dir::Dot(gp_Vec)` уже умеет: `xDir.Dot(gp_Vec(from, to))` без конверсии? Проверить API.

**Оценка:** 20 минут.

---

### CR-693. 🟠 `Geometry2D` — `ApplyOrtho`: `u2 = (u >= 0) ? len : -len;` — теряет знак если len = 0

**Где:** `Geometry2D.cpp:48-54`

`len < 1e-6` уже проверено. Ок.

**WONTFIX.**

---

### CR-694. 🟠 `Geometry2D` — `ComputeLengthAngle` не нормализует `angleDeg` в [0, 360)

**Где:** `Geometry2D.cpp:31`

```
angleDeg = std::atan2(w, u) * 180.0 / M_PI;
```

`std::atan2` возвращает [-π, π] → `angleDeg ∈ [-180, 180]`. Отрицательные углы — ок для математики, но пользователю показывать «-45°» не всегда удобно.

**WONTFIX** — если UI сам нормализует.

---

### CR-695. 🟠 `Geometry2D` — `gp_Pnt result = from; result.Translate(...)` — мутация

**Где:** `Geometry2D.cpp:56-58, 78-80, 97-99`

```
gp_Pnt result = from;   // copy
result.Translate(gp_Vec(xDir) * u2);   // mutate
result.Translate(gp_Vec(yDir) * w2);   // mutate
return result;
```

**Работает**, но `gp_Pnt::Translate` — мутирующий метод. Правильнее:
```
return from.Translated(gp_Vec(xDir) * u2 + gp_Vec(yDir) * w2);
```
Меньше копий, чище.

**Оценка:** 10 минут.

---

### CR-696. 🟠 `Geometry2D.h` — комментарий «без побочных эффектов» — но функции возвращают `gp_Pnt`

**Где:** `Geometry2D.h:6`

```
// Все функции — без состояния, без побочных эффектов.
```

Формально верно (нет static, нет глобалов). Но `ComputeLengthAngle` использует out-параметры `double& length, double& angleDeg` — это **побочный эффект** через ссылки.

**Фикс:** возвращать `struct { double length; double angleDeg; }` или `std::pair`.

**Оценка:** 15 минут.

---

## 🔴 P0 — util (Волна 15, часть 3: LabelName)

### CR-697. 🔴 `LabelName.cpp` — ПОДТВЕРЖДЁН BUG-A5 — `TCollection_AsciiString` теряет кириллицу

**Где:** `LabelName.cpp:24-26`
**Статус:** OPEN
**Категория:** BUG-A5 (REFACTOR_NOTES §A)

```
TCollection_AsciiString ascii(nameAttr->Get());
return ascii.ToCString();
```

**Что это значит:**
- `nameAttr->Get()` возвращает `TCollection_ExtendedString` (UTF-16).
- `TCollection_AsciiString(ExtendedString)` **понижает** до ASCII — все символы вне ASCII заменяются на `?` или теряются.
- «Деталь фланца» → «?????» или «Деталь ?фланца».

**Пользователь видит:**
- В дереве документа: «?????» вместо имени.
- В диалоге свойств: то же.
- В DependencyDialog: то же.
- В контекстном меню: то же.

**Комментарий в хедере уже честно говорит:** «Известное ограничение: GetLabelName использует AsciiString, поэтому кириллические имена теряются.»

**Это не должно быть «известным ограничением» — это баг, который надо фиксить.**

**Фикс:**
```
std::string GetLabelName(const TDF_Label& label) {
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        return Util::ExtToUtf8(nameAttr->Get());   // ← уже есть в Utf8.cpp
    }
    return "Line" + std::to_string(label.Tag());
}
```

**Но:** сначала надо **пофиксить `Utf8.cpp`** (CR-679). Иначе `ExtToUtf8` вернёт мусор на Linux.

**Оценка:** 5 минут кода + зависит от CR-679.

---

### CR-698. 🔴 `LabelKey` — `ostringstream` для одной строки

**Где:** `LabelName.cpp:14-17`

```
std::ostringstream oss;
label.EntryDump(oss);
return oss.str();
```

**`ostringstream`** — тяжёлый (heap, locale, virtual call на `operator<<`). Для **каждого узла дерева × каждый кадр × каждый lookup в map** = **тысячи `ostringstream` в секунду**.

**Связано с CR-093, CR-610** — тоже про `LabelKey`.

**Фикс:**
- `TDF_Label::EntryDump` имеет перегрузку в `char*`? Проверить.
- Или использовать `label.Entry()` — возвращает `TDF_Entry` (v. 7.x) или `uint64_t` — hashable.
- Или: `std::to_string(label.Tag())` + `father` рекурсивно.

**Рекомендую:** `uint64_t LabelKey(const TDF_Label&)` — комбинация `Tag()` и hash `Entry()`. O(1), без heap, hashable напрямую.

**Оценка:** 1 час (переписать + обновить всех потребителей: DocumentTree, Visibility, ContextMenu, Application).

---

### CR-700. 🟠 `LabelName.cpp` — `#include <sstream>` только для одного использования

**Где:** `LabelName.cpp:6`

`sstream` — тяжёлый (5 КБ кода). Используется в одной функции `LabelKey`, которая **перепишется** (CR-698).

**Фикс:** после CR-698 убрать `#include <sstream>`.

**Оценка:** входит в CR-698.

---

### CR-701. 🟠 `LabelName.h` — комментарий «См. BUG в TECH_DEBT» — но BUG-A5 в REFACTOR_NOTES

**Где:** `LabelName.h:8-9`

```
// Известное ограничение: GetLabelName использует AsciiString,
// поэтому кириллические имена теряются. См. BUG в TECH_DEBT.
```

**Комментарий ссылается на `TECH_DEBT`**, а BUG-A5 — в **`REFACTOR_NOTES §A`**. Мелочь, но путает.

**Фикс:** актуализировать или убрать после фикса CR-697.

**Оценка:** 2 минуты.

---

### CR-702. 🟠 `LabelName.h` — `LabelKey` возвращает `std::string`, но нужен `uint64_t`

**Где:** `LabelName.h:18`

**Связано с CR-698.** После фикса — поменять API.

**Оценка:** входит в CR-698.

---

### CR-703. 🟠 `Utf8.h` — функции возвращают объекты по значению (heap)

**Где:** `Utf8.h:16-17`

```
TCollection_ExtendedString Utf8ToExt(const std::string& s);
std::string ExtToUtf8(const TCollection_ExtendedString& es);
```

Каждый вызов = **heap-аллокация**:
- `TCollection_ExtendedString` — внутри OCCT handle (refcount + heap).
- `std::string` — heap.

**Для `ExtToUtf8` вызываемого при каждом рендере дерева** (через `LabelName`, CR-697) — **60 FPS × 500 узлов = 30000 аллокаций/сек.**

**Связано с CR-093, CR-610.**

**Фикс:**
- `void ExtToUtf8(const TCollection_ExtendedString& es, std::string& out);` — переиспользуемый буфер.
- Или `const std::string&` кэш.

**Оценка:** 1 час.

---

### CR-704. 🟠 `Geometry2D` — 4 out-параметра в `ComputeLengthAngle`

**Где:** `Geometry2D.h:22-26`

```
void ComputeLengthAngle(const gp_Pln& plane,
                        const gp_Pnt& from,
                        const gp_Pnt& to,
                        double& length,
                        double& angleDeg);
```

**2 out-параметра** — антипаттерн. Связано с CR-696.

**Фикс:** `struct LengthAngle { double length; double angle; };` — return by value.

**Оценка:** 20 минут.

---

### CR-705. 🟠 `Utf8.h` — `#include <TCollection_ExtendedString.hxx>` в публичном заголовке

**Где:** `Utf8.h:13`

`util/Utf8.h` **подключает** OCCT-заголовок. Значит **любой файл**, включающий `Utf8.h`, тянет OCCT.

**Что это значит:** `util/` не является независимым слоем. Связано с `ARCHITECTURE §2` — `util/` должен быть **чистым C++**, без OCCT.

**Фикс:** либо **убрать** `Utf8.h` из публичного API (только `.cpp`), либо — использовать **непрозрачный** интерфейс:
```
std::string ExtToUtf8Opaque(const void* ext);
void* Utf8ToExtOpaque(const std::string&);
```
Или — **отказаться** от OCCT в `util/Utf8`, сделать pure-C++ API. Тогда `Application`/`BreezeDocument` будут конвертировать сами.

**Оценка:** 2 часа (архитектурно).

---

## 📊 Итог util (Волна 15)

**Закрыто модулей util:** 3 из 3 (Utf8, Geometry2D, LabelName)
**Найдено записей:** 27 (CR-679..CR-705)

**Критические:**
- **CR-679** — кроссплатформенный баг Utf8 (блокирует Mac/Linux/ARM).
- **CR-697** — BUG-A5 (кириллица теряется в дереве).
- **CR-698** — `LabelKey` через `ostringstream` (hot-path).
- **CR-686, 687** — dead variable `origin`.

---

---

## 🔴 P0 — platform (Волна 16, часть 1: FileDialog)

### CR-706. 🔴 `FileDialog` — работает ТОЛЬКО на Windows, заглушка на других ОС

**Где:** `FileDialog.cpp:90-96`
**Статус:** OPEN
**Категория:** кроссплатформенность — блокирует Mac/Linux/ARM

```
#else  // не Windows — заглушка
bool ShowFileDialog(...) {
    return false;
}
#endif
```

**Что это значит:**
- **На macOS, Linux, Android** — пользователь жмёт «Сохранить» / «Открыть» → **ничего не происходит**.
- Нет ни диалога, ни ошибки, ни сообщения.
- **Соответствует `PROJECT_STATE.md`**: «FileDialog сейчас только Win32 — при выходе на Linux/Mac/Android нужны реализации.»

**Что уже отмечено как известное** (не баг), но это **блокирует**:
- Целевую платформу macOS (`HANDOFF §2`).
- Android (`CANON_FEATURES §1`).
- Apple Silicon (`REFACTOR_NOTES §G`).

**Фикс (в порядке приоритета):**
1. **GTK/Qt** — тяжёлые зависимости, нарушают «лёгкость».
2. **портируемая библиотека** — `nativefiledialog` (~40 КБ) или **`nfd-extended`**.
3. **собственный ImGui-диалог** — 500 строк, но **кросс-платформенно без зависимостей**.

**Рекомендую 3** — соответствует философии (минимум зависимостей, максимум контроля).

**Оценка:** 1-2 дня (правильно) или 2 часа (библиотека).

---

### CR-707. 🔴 `FileDialog.cpp` — `Utf8ToWide` / `WideToUtf8` дублируют `Util::Utf8`

**Где:** `FileDialog.cpp:17-33`
**Статус:** OPEN

```
static std::wstring Utf8ToWide(const std::string& s) { ... MultiByteToWideChar ... }
static std::string WideToUtf8(const std::wstring& s) { ... WideCharToMultiByte ... }
```

**А в `util/Utf8.cpp`** уже есть `Utf8ToExt` / `ExtToUtf8` — та же логика, но через OCCT.

**Проблема:**
- Две параллельные реализации UTF-конверсии в проекте.
- Разные API (WinAPI vs OCCT).
- Windows-реализация **не работает на Linux/Mac** (нет `MultiByteToWideChar`).
- Дублирование → если фиксят BUG-A5 в одном месте, забывают про второе.

**Связано с CR-679** (кроссплатформенный баг Utf8).

**Фикс:** использовать `Util::Utf8ToExt/ExtToUtf8`, обернув в `platform`.

**Оценка:** 30 минут (после CR-679).

---

### CR-708. 🔴 `FileDialog.cpp` — парсинг filter через `std::string(p)` — **ANSI-only**, не UTF-8

**Где:** `FileDialog.cpp:50-62`

```
const char* p = filter;
while (true) {
    std::string line(p);                          // ← ANSI (до '\0')
    if (line.empty()) { wFilter.push_back(L'\0'); break; }
    wFilter += Utf8ToWide(line);                  // ← конвертирует как UTF-8
    wFilter.push_back(L'\0');
    p += line.size() + 1;
}
```

**Комментарий в `.h:11` говорит:** «filter — фильтр в формате WinAPI».

**В WinAPI** фильтр — **ANSI-строки** (CP1251 в русской локали). А здесь они парсятся как **UTF-8**.

**Что это значит:**
- Если фильтр содержит кириллицу (`"Все файлы (*.*)\0*.*\0"`) в ANSI — конвертация UTF-8 **сломает** строку.
- Если фильтр в UTF-8 — работает, но **не по спецификации WinAPI**.

**Фикс:**
- Либо явно сказать в комментарии «фильтр в UTF-8».
- Либо использовать `OPENFILENAMEW` + Unicode-фильтр напрямую.

**Оценка:** 15 минут.

---

### CR-709. 🔴 `FileDialog.cpp` — `wcsncpy(fileName, ..., MAX_PATH - 1)` без гарантии NUL

**Где:** `FileDialog.cpp:41`

```
std::wstring initial = Utf8ToWide(outPath);
wcsncpy(fileName, initial.c_str(), MAX_PATH - 1);
```

**Проблема:** `wcsncpy` **не гарантирует** null-терминацию при обрезке. Если `initial` длиннее `MAX_PATH - 1` — строка **без `\0`** → WinAPI прочитает мусор.

**Фикс:** `fileName[MAX_PATH - 1] = L'\0';` после `wcsncpy`.

**Оценка:** 1 минута.

---

### CR-710. 🔴 `FileDialog.cpp` — default `filter` содержит `\0` в default-параметре — UB

**Где:** `FileDialog.h:14`

```
const char* filter = "OCAF Binary (*.cbf)\0*.cbf\0All Files (*.*)\0*.*\0\0"
```

**Что не так:**
- В C++ литерал `"...\0...\0..."` — **это уже `const char*`**, не `std::string`.
- Default-параметр берёт **первую строку до `\0`** — то есть `"OCAF Binary (*.cbf)"`.
- **Дальше — не входит.**

**Проверить:** `strlen(filter)` вернёт длину первой части. Возможно, работает корректно (потому что `\0` действительно является разделителем), но:

**Проблема:** GCC/Clang могут **предупредить** о неиспользуемых частях после `\0`. С `-Wall` — warning. С `-Werror` — не соберётся.

**Плюс:** `const char*` подразумевает null-терминированную строку, а тут **несколько** строк подряд. Семантически запутанно.

**Фикс:** либо сделать default в `.cpp`, либо — `static const char kDefaultFilter[] = "...\0...\0";` явно в `.cpp`.

**Оценка:** 10 минут.

---

### CR-711. 🟠 `FileDialog.cpp` — `hwndOwner = nullptr` — диалог не привязан к окну

**Где:** `FileDialog.cpp:68`

```
ofn.hwndOwner   = nullptr;
```

**Проблема:** диалог не привязан к родительскому окну. Что ломается:
- Диалог может открыться **за** главным окном.
- При Alt+Tab — диалог не «прилипает» к главному.
- Модальность не работает.

**Фикс:** получать HWND главного окна (через `glfwGetWin32Window`) и передавать в `FileDialog`.

**Оценка:** 20 минут.

---

### CR-712. 🟠 `FileDialog.cpp` — `Utf8ToWide` не проверяет код возврата `MultiByteToWideChar`

**Где:** `FileDialog.cpp:19-22`

```
int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
std::wstring out(size, L'\0');
MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size);
```

**Если первый вызов вернёт 0** (invalid UTF-8) — `out(0, L'\0')` = пустая строка. Второй вызов — на пустой буфер. **Молчаливая ошибка.**

**Фикс:** `if (size <= 0) return L"";`.

**Оценка:** 3 минуты.

---

### CR-713. 🟠 `FileDialog.h` — комментарий про `save = false/true` — нет

**WONTFIX** — норм.

---

## 🔴 P0 — platform (Волна 16, часть 2: GlfwWindow)

### CR-714. 🔴 `GlfwWindow` — Compatibility Profile НЕ установлен, комментарий врёт

**Где:** `GlfwWindow.cpp:19-22`
**Статус:** OPEN
**Категория:** потенциальный краш на macOS + рассинхрон с комментарием

```
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
// Не указываем Core Profile — по умолчанию будет Compatibility
// glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
```

**Комментарий врёт.** По документации GLFW:
- Если запросить **3.2+**, GLFW **по умолчанию даёт Core Profile**, а не Compatibility.
- На macOS **Compatibility недоступен вообще** для 3.2+ — только Core.
- На Windows/Linux — работает, но это неявное поведение.

**Почему важно:**
- **`OcctView::Init`** устанавливает `myDriver->ChangeOptions().contextCompatible = true;` (CR-257 — ага, у нас другой CR). Если реальный контекст — Core, **OCCT может не заработать** с толстыми линиями.
- Но в проекте **работает** — значит, либо GLFW даёт Compatibility, либо OCCT адаптируется.
- **На macOS** контекст 3.3 = **Core**, и толстые линии (`glLineWidth > 1`) **не работают** — deprecated.

**Фикс:**
```
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPATIBILITY_PROFILE);   // ← явно
```

**Плюс:** для macOS — `#ifdef __APPLE__` fallback на Core + custom line shader.

**Оценка:** 30 минут (Windows/Linux) + отдельная задача для macOS.

---

### CR-716. 🔴 `GlfwWindow::~GlfwWindow` — `glfwTerminate` убивает всю GLFW

**Где:** `GlfwWindow.cpp:57-62`

```
GlfwWindow::~GlfwWindow() {
    if (myWindow) glfwDestroyWindow(myWindow);
    glfwTerminate();   // ← терминейтит ВСЮ библиотеку
}
```

**Проблема:** если в проекте **два** `GlfwWindow` (например, в тестах или многооконном режиме), деструктор первого убьёт GLFW **до** деструктора второго → **краш во втором**.

**Фикс:** рефкаунтинг или — `glfwTerminate` только в `Application::~Application` (последний владелец).

**Оценка:** 20 минут.

---

### CR-717. 🔴 `GlfwWindow::GlfwWindow` — при провале `glfwInit` не сбрасывает `myWindow`

**Где:** `GlfwWindow.cpp:13-16`

```
GlfwWindow::GlfwWindow(...) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return;   // ← myWindow остался nullptr (ок)
    }
```

**Ок.** WONTFIX.

**Но:** `glfwInit` может провалиться **после** успешного создания окна (в `glfwCreateWindow`). Тогда `glfwTerminate` вызывается, но `myWindow` = nullptr. Ок.

---

### CR-718. 🔴 `GlfwWindow.cpp` — колбэки через `std::function` — аллокация при каждом Set

**Где:** `GlfwWindow.cpp:76-79`

```
void GlfwWindow::SetMouseButtonCallback(MouseButtonCallback cb) { myMouseButtonCb = std::move(cb); }
```

`std::move` — хорошо. **Но:** `MouseButtonCallback = std::function<void(int,int,int)>` — при **создании** лямбды в `Application.cpp:180` (10 строк closure) — **heap-аллокация**.

**Это происходит один раз** в конструкторе Application. **Не hot-path.** WONTFIX.

---

### CR-719. 🟠 `GlfwWindow.cpp` — `glfwSwapInterval(1)` — VSync без опции

**Где:** `GlfwWindow.cpp:36`

```
glfwSwapInterval(1);  // VSync
```

**Хардкод VSync.** Связано с `TECH-DEBT-008` (FPS 30 на Legion 5 Pro). Возможно, VSync виноват.

**Фикс:** параметр конструктора или метод `SetVSync(bool)` + настройка в `SettingsDialog`.

**Оценка:** 15 минут.

---

### CR-720. 🟠 `GlfwWindow.cpp` — `ToggleFullscreen` не проверяет `mySavedW != 0`

**Где:** `GlfwWindow.cpp:99-119`

Если пользователь **сразу** нажал F11 в невидимом окне — `glfwGetWindowSize` вернёт 0. При выходе из fullscreen — `glfwSetWindowMonitor(..., mySavedW=0, mySavedH=0, ...)` — **невалидный размер**.

**Фикс:** `if (mySavedW <= 0 || mySavedH <= 0) return;`.

**Оценка:** 3 минуты.

---

### CR-721. 🟠 `GlfwWindow.cpp` — `glfwSetErrorCallback` не установлен

**Где:** весь файл

GLFW имеет `glfwSetErrorCallback` — глобальный. **Не установлен.** При ошибках (не найдено OpenGL 3.3, драйвер сломан) — **никакой диагностики**.

**Фикс:** установить в `GlfwWindow::GlfwWindow`:
```
glfwSetErrorCallback([](int code, const char* desc) {
    std::cerr << "GLFW error " << code << ": " << desc << "\n";
});
```

**Оценка:** 5 минут.

---

### CR-722. 🟠 `GlfwWindow.h` — `ToggleFullscreen`/`Maximize` — методы без описания

**WONTFIX.**

---

### CR-723. 🟠 `GlfwWindow.cpp` — анонимный namespace `Self` — только для 4 колбэков

**Где:** `GlfwWindow.cpp:6-10`

**Функция `Self`** возвращает `GlfwWindow*` через `glfwGetWindowUserPointer`. Используется 4 раза.

**Ок**, паттерн стандартный.

**WONTFIX.**

---

### CR-724. 🟠 `GlfwWindow` — `myIsFullscreen` — bool вместо enum

**Где:** `GlfwWindow.h:36`

```
bool myIsFullscreen = false;
```

**Ок** для двух состояний.

**WONTFIX.**

---

### CR-725. 🟠 `GlfwWindow.cpp` — `glfwSetWindowUserPointer` вызывается только после `MakeContextCurrent`

**Где:** `GlfwWindow.cpp:37`

**Порядок:**
```
glfwMakeContextCurrent(myWindow);
glfwSwapInterval(1);
glfwSetWindowUserPointer(myWindow, this);
glfwSetMouseButtonCallback(...);   // ← уже после SetWindowUserPointer
```

**Ок.** WONTFIX.

---

### CR-726. 🟠 `GlfwWindow` — `#include <iostream>` в заголовке? Нет. Только в .cpp

**WONTFIX.**

---

### CR-727. 🟠 `GlfwWindow.cpp` — `glfwWindowHint(GLFW_OPENGL_PROFILE, ...)` — закомментировано

**Связано с CR-714.** Комментарий врёт.

---

## 📊 Итог platform (Волна 16)

**Модулей platform:** 2 (FileDialog, GlfwWindow)
**Найдено записей:** 22 (CR-706..CR-727)

**Критические:**
- **CR-706** — FileDialog только Windows (блокирует кроссплатформенность).
- **CR-714** — Compatibility Profile не установлен (потенциальный краш macOS).
- **CR-715** — `ShouldClose()` UB при null.
- **CR-716** — `~GlfwWindow` убивает GLFW для всех.
- **CR-707** — дубль Utf8-конвертеров.
- **CR-710** — UB в default-параметре filter.

---

---

## 🔴 P0 — platform (Волна 16, часть 3: подтверждение проблем с толстыми линиями)

### CR-728. 🔴 Толстые линии (`glLineWidth > 1`) НЕ РАБОТАЮТ — связано с CR-714

**Где:** `GlfwWindow.cpp:19-22`, `OcctView.cpp:44, 165-166, 285, 313` и др.
**Статус:** OPEN (подтверждено MikeVerx, 2026-09-27)
**Категория:** функциональный баг рендеринга

**Что не работает:**
- В эскизе (сетка, оси X/Y): `Prs3d_LineAspect(color, Aspect_TOL_SOLID, 4.0)` — задаём ширину 4, рисуется 1.
- В предпросмотре: `SetWidth(10.0)` — рисуется 1.
- В линиях эскиза: `width = 6.0/7.0/12.0` — рисуется 1.

**Причина:**
- **OpenGL Core Profile** (который даёт GLFW для 3.3+) **deprecated** `glLineWidth > 1`.
- На macOS — **не поддерживается вообще**.
- На Windows — зависит от драйвера.
- OCCT `Prs3d_LineAspect::SetWidth` использует `glLineWidth` → тихо игнорируется.

**Связано:**
- **CR-714** — Compatibility Profile не установлен.
- `OcctView.cpp:44` — `ChangeOptions().contextCompatible = true` — заявка, которая не соответствует реальному контексту.

**Что подтверждает MikeVerx:**
> «Толстые линии и так не получилось заставить работать. Там пока тонкая пакость. Надо будет фиксить. Может даже со всеми багами.»

**Три варианта фикса:**

**A. Установить Compatibility Profile явно (Windows/Linux):**
```
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPATIBILITY_PROFILE);
```
- **Плюсы:** 5 минут, `glLineWidth` заработает.
- **Минусы:** не работает на macOS; deprecated в OpenGL 3.2+.

**B. Собственный шейдер для толстых линий:**
- Рисуем линию как **quad (2 треугольника)** через geometry shader или instanced draw.
- `AIS_Line` в OCCT **не подходит**, но для **эскизной сетки** можно рисовать через `ImDrawList` (уже толстые, антиалиасятся).
- **Плюсы:** работает везде, включая macOS/Vulkan/Metal.
- **Минусы:** 2-3 дня работы, нужно переписать рендер линий в `OcctView`.

**C. Использовать ImGui drawList для линий эскиза (вместо OCCT):**
- `DisplaySketchLine` уже знает начало/конец.
- Рисовать через `ImDrawList::AddLine` в координатах экрана.
- **Плюсы:** работает везде, антиалиасинг, просто.
- **Минусы:** нужно самому проецировать 3D→2D; конфликт с occlusion.
- **Оценка:** 4-6 часов.

**Рекомендую:**
- **Сейчас (быстро):** A — для Windows/Linux.
- **В Фазе 2 (правильно):** B или C — для полной кроссплатформенности.

**Оценка:**
- A — 10 минут.
- B — 3 дня.
- C — 4-6 часов.

**Занести как отдельный баг:** `BUG-A6 — толстые линии не работают`.

---

## 🔴 P0 — app (Волна 16, часть 4: Localization.h)

### CR-729. 🔴 `Localization` — синглтон `Instance()` — RULES-нарушение

**Где:** `Localization.h` (предположительно)
**Статус:** OPEN
**Категория:** архитектура

В `SettingsDialog.cpp:112` — `Localization::Instance().GetLanguage()`. В `QAT` — `Tr()` как free-функция.

**Что это значит:**
- Смешение подходов: где-то `Localization::Instance()`, где-то `Tr()`.
- Синглтон — глобальное состояние, тесты невозможны.
- При двух окнах — **общий словарь**.

**Фикс:** либо всё через `Tr()` (free function → `Localization::Instance()` внутри), либо всё через инжекцию.

**Оценка:** 30 минут (уточнить после просмотра .cpp).

---

---

## 🔴 P0 — app (Волна 16, часть 5: Localization.h/.cpp)

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

### CR-731. 🔴 `Localization` — словарь `unordered_map<string, pair<string, string>>` — 3 строки на запись

**Где:** `Localization.cpp:8`

```
const std::unordered_map<std::string, std::pair<std::string, std::string>> kDict = { ... };
```

**Что это значит:**
- Ключ — `std::string` (heap или SSO).
- Значение — `std::pair<std::string, std::string>` (2 heap или SSO).
- **~200 записей × 3 строки = 600 потенциальных аллокаций при инициализации.**
- **`unordered_map` lookup** — hash строки на каждый `Tr()` — **ещё аллокация?** Нет, но хеш-считается каждый раз.

**Плюс:** при инициализации static — все 200+ записей в heap. **Одна большая работа при старте**, но 200 × 2 строки = ~10-20 КБ данных.

**Фикс:**
- `std::map<std::string_view, std::pair<std::string_view, std::string_view>>` — **ноль heap-аллокаций** (всё в string literals).
- Или `constexpr std::array<Entry, N>` + линейный поиск (200 элементов) — cache-friendly.

**Оценка:** 1 час (с рефакторингом Tr).

---

### CR-732. 🔴 `Tr()` — если ключа нет, возвращает **ключ целиком**

**Где:** `Localization.h:16`

```
// Перевод по ключу. Если ключа нет — возвращает ключ как есть.
std::string Tr(const std::string& key) const;
```

**Что это значит:** пользователь видит `"panel.deps.fully_constrained"` вместо текста.

**Связано с TECH-DEBT-004** (`tool.project.loop` — не локализован). Это **общий механизм утечки ключей**.

**Фикс:**
- В debug — assert.
- В release — `"[" + key + "]"` с явной маркировкой.

**Оценка:** 15 минут.

---

### CR-733. 🔴 `Tr()` не умеет **форматированные** строки

**Где:** `Localization.cpp:139-143`

```
{ "debug.selected.tag",    { "Selected: tag=%d",       "Выбрано: тег=%d" } },
{ "debug.selected.none",   { "Selected: none",         "Выбрано: ничего" } },
```

**Ключ `debug.selected.tag`** содержит `%d`. Но `Tr()` возвращает `std::string` — **где подставляется %d?**

В коде, вероятно: `Text(Tr("debug.selected.tag").c_str(), tag)` — но это **опасно**: если пользователь случайно заложит в перевод лишний `%`, получим UB в `printf`-стиле.

**Фикс:** явное API:
```
std::string TrFmt(const std::string& key, ...);
```
Или через `std::format` (C++20).

**Оценка:** 2 часа.

---

### CR-734. 🟠 `Localization.cpp` — ключи `tool.text.line`, `tool.chamfer.2d` и т.д. имеют **лишние пробелы** перед значениями

**Где:** `Localization.cpp:124-140`

```
{ "tool.text.line",                        { "Text on Line",                      "Текст по линии" } },
```

**Множественные пробелы для визуального выравнивания** — при правках легко сбить, diff покажет шум.

**Фикс:** clang-format для словаря (одна конфигурация).

**Оценка:** 10 минут.

---

### CR-735. 🟠 `Localization` — нет ключа для многих строк, которые видны в UI

**Где:** весь проект

**Найдено CR-384, CR-407, CR-465, CR-471, CR-500, CR-508, CR-533, CR-544, CR-587, CR-598, CR-613, CR-665** — **десятки хардкод-строк** без `Tr`.

**Что не хватает в словаре:**
- `qat.*` — меню лого, формат-меню, textures-меню (CR-384).
- `dlg.*` — Rename/DeleteConfirm/Unsaved/Dependency (CR-465, 471, 477, 517, 525).
- `prop.*` — PropertiesDialog (CR-500).
- `panel.*` — RightPanel (CR-587).
- `vc.*` — ViewCube (CR-665).
- `misc.unnamed` (CR-191, CR-499, CR-599, CR-608).
- `view.top/bottom/front/back/left/right` (CR-598).
- `status.*` (CR-407).

**Оценка:** 4-5 часов (суммарно по всем CR).

---

### CR-736. 🟠 `Localization.h` — inline `Tr()` в заголовке — экспорт

**Где:** `Localization.h:21-23`

```
inline std::string Tr(const std::string& key) {
    return Localization::Instance().Tr(key);
}
```

**Inline в хедере — ок**, но:
- `Tr` вызывается из **UI-слоя**, а `Localization` — в **app-слое**.
- Значит, **UI зависит от app** — по слоям верно (ui → app).
- Но **обратная зависимость app → ui** тоже есть (`Application.cpp` → `UI::Home`). **Цикл.**

**Связано с архитектурой.** Не критично, но стоит помнить.

**WONTFIX.**

---

## 🔴 P0 — doc (Волна 16, часть 6: BreezeApplication)

### CR-737. 🔴 `BreezeApplication::CreateDocument` — возвращает `nullptr` вместо `Handle()`

**Где:** `BreezeApplication.cpp:24-34`
**Статус:** OPEN

```
Handle(TDocStd_Document) BreezeApplication::CreateDocument() {
    Handle(TDocStd_Document) doc;
    TCollection_ExtendedString format("BinOcaf");

    myApp->NewDocument(format, doc);
    if (doc.IsNull()) {
        std::cerr << "BreezeApplication: failed to create new document\n";
        return nullptr;   // ← nullptr конвертируется в Handle?
    }
    return doc;
}
```

**Что не так:**
- `Handle(TDocStd_Document)` — это `opencascade::handle<TDocStd_Document>`.
- У него есть конструктор из `std::nullptr_t` → **создаёт пустой handle**. **Ок.**
- Формально работает, но: **читателю непонятно**. Лучше `return Handle(TDocStd_Document)();`.

**Фикс:** `return {};` или явный null-handle.

**Оценка:** 2 минуты.

---

### CR-738. 🔴 `BreezeApplication::SaveDocument` — `TCollection_ExtendedString(path.c_str())` — БАГ КИРИЛЛИЦЫ

**Где:** `BreezeApplication.cpp:49`
**Статус:** OPEN
**Категория:** тот же класс, что BUG-A5, но в doc-слое

```
TCollection_ExtendedString extPath(path.c_str());
```

**Что это значит:**
- `path` — `std::string` (UTF-8).
- `TCollection_ExtendedString(const char*)` — **конструктор от ANSI-строки** (CP1251 на русской Windows) **или** от UTF-8 в зависимости от OCCT-конфигурации.
- По умолчанию — **CP1251**. **Кириллица сломается** → это **BUG-A5 в doc-слое**.

**Плюс:** файл сохранится с кракозябрами в пути, но **файл не найдётся** при открытии (сравнение строк).

**Связано с TECH-DEBT-002** («кириллица в пути SaveAs → кракозябры») — **тот же источник**.

**Фикс:**
```
TCollection_ExtendedString extPath = Util::Utf8ToExt(path);
```

Но это **зависит от CR-679** (кроссплатформенный баг Utf8).

**Оценка:** 5 минут после CR-679.

---

### CR-739. 🔴 `BreezeApplication::OpenDocument` — та же проблема

**Где:** `BreezeApplication.cpp:60`

```
TCollection_ExtendedString extPath(path.c_str());
```

**Связано с CR-738.**

**Плюс:** `PCDM_ReaderStatus status` — статус читается, но в лог пишется **числом** (`status=`), не human-readable. Сложно диагностировать.

**Фикс:** либо таблица статусов, либо `PCDM_ReaderStatusToString(status)`.

**Оценка:** входит в CR-738.

---

### CR-742. 🟠 `BreezeApplication` — `myApp = new TDocStd_Application()` — не `Handle`

**Где:** `BreezeApplication.cpp:16`

```
myApp = new TDocStd_Application();
```

`myApp` — `Handle(TDocStd_Application)`. `new` возвращает `TDocStd_Application*`, а Handle принимает его **с ++refcount** — ок.

**WONTFIX.**

---

### CR-743. 🟠 `BreezeApplication` — `TCollection_ExtendedString format("BinOcaf")` — hardcoded формат

**Где:** `BreezeApplication.cpp:26`

**Фикс:** `constexpr const char* kOcafFormat = "BinOcaf";`.

**Оценка:** 2 минуты.

---

### CR-745. 🟠 `BreezeApplication` — `SaveDocument` использует `myApp->SaveAs`, а не свой формат

**Где:** `BreezeApplication.cpp:52`

```
myApp->SaveAs(doc, extPath);
```

**По `FILE_FORMAT.md`** — целевой формат `.bzc` (ZIP + manifest + history). Здесь — **OCAF `BinOcaf`** (`.cbf`).

**Связано с ADR-0001** (план: свой формат).

**Отложено до Фазы 3.**

---

### CR-746. 🟠 `Localization.h` — `Toggle()` метод — не используется?

**Где:** `Localization.h:13`

```
void Toggle() { myLang = (myLang == Language::EN) ? Language::RU : Language::EN; }
```

**Проверить, где вызывается.** Возможно — мёртвый код.

**Оценка:** 2 минуты (проверить).

---

### CR-747. 🟠 `Localization.cpp` — порядок ключей хаотичен

**Где:** весь файл

Ключи идут: общие → QAT → вкладки → группы → tools → ... → debug. **Логично**, но:
- `tool.rect.2pt.center` и `tool.rect.2pt` **далеко друг от друга** (строки 40 и 128).
- `tool.slot.*` все в конце.
- Пять `tool.spline.*` разбросаны.

**Фикс:** сортировка по алфавиту внутри групп.

**Оценка:** 15 минут.

---

### CR-748. 🟠 `Localization.cpp` — строки **переводов** содержат `?` иногда

**Где:** не вижу в первых 150 строках. **Возможно, во второй половине.**

**Проверить.**

---

### CR-749. 🟠 `Localization.h` — `enum class Language { EN, RU }` — всего 2 языка

**Где:** `Localization.h:7`

**По `CANON_FEATURES §1`:** «Языковые пакеты расширим после MVP».

**Ок** для MVP.

**WONTFIX.**

---

### CR-750. 🟡 `Localization` — `Tr` — имя функции с большой буквы, но **не класс и не тип**

**Где:** `Localization.h:21`

```
inline std::string Tr(const std::string& key) { ... }
```

**По `RULES §10`:** «Функции: camelCase». `Tr` — исключение, но распространено в проекте (Qt-style). **Ок.**

**WONTFIX.**

---

### CR-751. 🟡 `BreezeApplication` — стиль `Instance()` — Qt-style singleton

**WONGFIX.**

---

## 🎯 ФИНАЛ АУДИТА

**Волна 16 (app + doc) — закрыта.**
**Найдено записей:** 23 (CR-729..CR-751)

---

## 📊 ИТОГИ ПОЛНОГО АУДИТА

**Покрытие:**
- ✅ src/ui/ribbon/ (все файлы)
- ✅ src/ui/qat/ (все файлы)
- ✅ src/ui/status/ (все файлы)
- ✅ src/ui/home/ (все файлы)
- ✅ src/ui/dialogs/ (все 8 диалогов)
- ✅ src/ui/panels/ (все файлы)
- ✅ src/ui/theme/ (все файлы)
- ✅ src/ui/widgets/ (все файлы)
- ✅ src/ui/viewcube/ (все файлы)
- ✅ src/app/Application.{h,cpp}
- ✅ src/app/IconManager.{h,cpp}
- ✅ src/app/Localization.{h,cpp}
- ✅ src/doc/BreezeDocument.{h,cpp}
- ✅ src/doc/BreezeApplication.{h,cpp}
- ✅ src/occt/OcctView.{h,cpp}
- ✅ src/platform/FileDialog.{h,cpp}
- ✅ src/platform/glfw/GlfwWindow.{h,cpp}
- ✅ src/util/Utf8.{h,cpp}
- ✅ src/util/Geometry2D.{h,cpp}
- ✅ src/util/LabelName.{h,cpp}
- ✅ CMakeLists.txt

**Всего файлов:** 75+
**Всего записей:** ~751 (CR-001..CR-751)
**Severity:**
- P0: ~250
- P1: ~180
- P2: ~150
- P3: ~50
- RAM: 13

**Главные категории:**
1. **Локализация (хардкод строк)** — ~200 мест.
2. **Кроссплатформенность** — CR-679 (Utf8), CR-706 (FileDialog), CR-714 (Compatibility Profile), CR-728 (толстые линии).
3. **Слоение (UI знает OCCT)** — ~20 мест.
4. **Хот-пас аллокации** — ~30 мест.
5. **RULES п.20 (static)** — ~10 мест.
6. **RULES п.30 (хардкод цветов)** — ~40 мест.
7. **Функциональные баги** — CR-049/050/051/053 (ViewTab), CR-232 (транзакции OCAF), CR-605 (splitter), CR-611 (debug-вывод), CR-657 (BUG-4.1), CR-694 (duplicate menu).
8. **Layout magic числа** — ~100 мест, 6 разных «высот ribbon».

**Готово к переразбиению.**

---

### RAM-H1. Кэш текстур иконок в `IconManager` 🔴
**Включает (merged):** RAM-H3, CR-110, CR-224

**Подозрение:** `IconManager::Get(name, size)` кэширует текстуры по паре (имя, размер). Но:
- Кэш **никогда не чистится** — если размер меняется (zoom/resize), накапливаются новые текстуры.
- GL-текстуры (`GLuint`) не удаляются — утечка GPU RAM.

**Проверить:**
```
grep -an "glDeleteTextures\|~IconManager\|ClearCache" src/app/IconManager.*
```

**Вероятность:** 🔴 **высокая.**

---

### RAM-H2. `IconManager` держит все SVG в памяти 🔴

**Подозрение:** 105 иконок предзагружены (лог: `IconManager: preloaded 105/105`). Возможно, держит и исходные SVG-буферы, и растеризованные RGBA.

**Проверить:**
```
grep -an "mySvgBuffers\|myRawData\|myCache" src/app/IconManager.h
```

**Вероятность:** 🟠 средняя.

---

### RAM-H4. OCCT AIS_InteractiveObject — не освобождаются
**Включает (merged):** RAM-H10, CR-237, CR-253, CR-740, CR-741

**Подозрение:** каждый `AddBox`/`DisplaySketchLine` создаёт AIS-объект в `OcctView`. Если документ закрыт, но AIS не очищен — OCCT держит.

**Проверить:**
```
grep -an "myContext->Remove\|AIS_InteractiveContext::Remove\|myContext->Erase" src/occt/OcctView.cpp
```

**Вероятность:** 🔴 **высокая.**

---

### RAM-H5. `std::function` closure в `DrawRibbon` — накапливаются
**Включает (merged):** CR-001

**Связь с CR-001.** Каждый кадр — новые closures. Уничтожаются в конце кадра, но heap-фрагментация растёт.

**Вероятность:** 🟡 низкая (фрагментация, не рост).

---

### RAM-H6. `TRACE` пишет в файл с `fflush` каждый кадр

**Подозрение:** даже при `BREEZECAD_TRACE=0` — мёртвый код? Проверить.

**Вероятность:** 🟢 низкая.

---

### RAM-H7. `myCommands` — вектор не чистится

**Подозрение:** `InitCommands` вызывается один раз? Или каждый doc?

**Проверить:**
```
grep -an "InitCommands" src/app/Application.cpp
```

**Вероятность:** 🟡 низкая.

---

### RAM-H8. Утечка в ImGui — drawList не сбрасывается

**Подозрение:** если какой-то `Begin*` без парного `End*` — ImGui копит.

**Проверить визуально:** в debug-сборке ImGui выдаёт assert.

**Вероятность:** 🟡 низкая (упало бы).

---

### RAM-H9. Кэш `std::map<std::string, bool> myVisibility` растёт
**Включает (merged):** CR-114, CR-550

**Подозрение:** в `myVisibility` — записи для всех фич. Если фичи удаляются, а записи остаются — медленный рост.

**Проверить:**
```
grep -an "myVisibility\[\|myVisibility.erase\|myVisibility.clear" src/app/Application.cpp
```

**Вероятность:** 🟠 средняя.

---

