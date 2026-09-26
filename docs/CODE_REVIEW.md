# BreezeCAD — Code Review (пост-мортем Фазы 1.5)

**Дата:** 2026-09-26
**Ревьюер:** чужой senior C++ разработчик (аудит с максимальным педантизмом)
**Область:** `src/ui/ribbon/`, `src/app/Application.{h,cpp}`
**Метод:** построчный аудит + оценка рациональности + проверка философии проекта

**Зачем этот файл:** после большого рефакторинга остаются долги. Здесь они все — с severity, статусом и планом. Файл ведётся до полного закрытия.

**Легенда severity:**
- 🔴 **P0 — критично** — фиксить сейчас, нарушает правила проекта / философию
- 🟠 **P1 — серьёзно** — фиксить до Фазы 2
- 🟡 **P2 — стиль** — фиксить при случае
- 🟢 **P3 — мелочи** — не срочно

**Статусы:** `OPEN` / `FIXED` / `DEFERRED` / `WONTFIX`

---

## Резюме

**Оценка кода Фазы 1.5: 6/10.**

Работает, слои соблюдены, функциональность не потеряна. Но:
- нарушены RULES п.20 (static) и п.30 (хардкод цветов);
- производительность в hot-path (~900 heap-аллокаций/сек);
- неконсистентность 4 «новых» вкладок vs 3 «старых»;
- ~15 стилевых проблем.

**Главный вывод:** Фаза 1.5 вынесла код из `Application.cpp`, но законсервировала внутренние долги под модульной обёрткой. Следующий шаг — устранить долги внутри модулей.

---

## 🔴 P0 — КРИТИЧНО

### CR-001. `std::function` пересоздаётся 60 раз/сек

**Где:** `Application.cpp::DrawRibbon()`, `RibbonContext.h`
**Статус:** OPEN
**Категория:** производительность (OPT)

**Проблема:** каждое поле `ctx.drawQat`, `ctx.clearPreviewLine`, `ctx.exitSketch` — это closure + heap-аллокация на `std::function`. Создаётся каждый кадр. Внутри `ctx.drawQat` ещё `QatContext` с 10 колбэками.

**Замеры:**
- ~15 closure'ов × 60 FPS = **900 heap-аллокаций/сек** только на ribbon
- +10 колбэков `qat.*` = ещё 600/сек

**Цена:** для целевого железа (4 ГБ, iGPU, ARM A18) — недопустимо.

**Фикс:**
1. `RibbonContext myRibbonContext` — **member** `Application`.
2. Инициализация — один раз в конструкторе или `InitCommands`.
3. В `DrawRibbon()` — только обновление мгновенных полей.

**Оценка:** 30–40 минут.

---

### CR-002. `static int dummy = 0` — RULES п.20 + мина

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

## 🟠 P1 — СЕРЬЁЗНО

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

### CR-016. `Application.cpp:534` — устаревший комментарий

**Где:** `Application.cpp:534`
**Статус:** OPEN

```
// Универсальная кнопка Ribbon с SVG-иконкой Lucide.
void Application::DrawRibbon() {   // ← уже не кнопка, а оркестратор
```

**Фикс:** убрать или актуализировать.

**Оценка:** 1 минута.

---

## 🟡 P2 — СТИЛЬ

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

### CR-021. `ImGuiIO& ioChild = ImGui::GetIO();` — дублирование

**Где:** `Ribbon.cpp:41`
**Статус:** OPEN

`io` уже объявлен в начале функции.

**Фикс:** использовать `io`.

**Оценка:** 1 минута.

---

### CR-022. `snprintf` без `<cstdio>`

**Где:** `SketchTab.cpp:172`
**Статус:** OPEN

Неявно приходит через `imgui.h`. Надо явно.

**Фикс:** `#include <cstdio>`.

**Оценка:** 1 минута.

---

### CR-023. `-Wall -Wextra` не включены в CMake

**Где:** `CMakeLists.txt`
**Статус:** OPEN

Если бы были — CR-015 (`startPos`) дал бы warning. Значит — нет.

**Фикс:** добавить `-Wall -Wextra -Wpedantic` для GCC.
**Предупреждение:** может вскрыть **много** нового — делать отдельным коммитом.

**Оценка:** 30 минут диагностики.

---

## 🟢 P3 — МЕЛОЧИ

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

## ✅ Что хорошо

- **Слои соблюдены:** `ui/ribbon/` не знает про `Application`.
- **`RibbonContext` передаётся по ссылке**, не копируется.
- **Include-стиль:** IWYU в `Ribbon.cpp` (свой `.h` первым).
- **Комментарии по делу**, без воды.
- **`#pragma once`** везде, не `#ifndef`.
- **Никаких `using namespace std;`.**
- **`constexpr` где можно** (`TARGET_H = 76.0f`).
- **Функциональность не сломана** после 7 подшагов — редкость.

---

## План устранения

### Сессия 1 (следующие 2-3 часа)
Быстрые + критические:
- [ ] CR-001 — `std::function` → member
- [ ] CR-002 — убрать `static dummy` + занести B-17
- [ ] CR-003 — хардкод цветов → тема
- [ ] CR-007 — удалить `drawTabs`
- [ ] CR-013 — удалить `oneOpt`
- [ ] CR-015 — удалить `startPos`
- [ ] CR-016 — устаревший комментарий
- [ ] CR-028 — ложный комментарий про B-17

**Итог:** 8 проблем закрыто, ribbon стал чище.

### Сессия 2 (до Фазы 2)
- [ ] CR-004 — LineStyle → колбэк
- [ ] CR-005 — ButtonOpts
- [ ] CR-006 — BtnResult enum
- [ ] CR-008 — nullptr-контракт
- [ ] CR-009 — control coupling
- [ ] CR-010 — splitMiddle → OPT-05

### Сессия 3 (перед Фазой 2)
- [ ] CR-011 — унификация вкладок
- [ ] CR-019 — `enum class Tab`
- [ ] CR-020 — 206 → константа
- [ ] CR-023 — `-Wall -Wextra -Wpedantic`

### Отложено
CR-012, CR-014, CR-017, CR-018, CR-021, CR-022, CR-024, CR-025, CR-026, CR-027, CR-029, CR-030, CR-031.

---

## Прогресс

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 4 | 0 |
| 🟠 P1 | 12 | 0 |
| 🟡 P2 | 7 | 0 |
| 🟢 P3 | 8 | 0 |
| **Всего** | **31** | **0** |

---

## Правило для нового чата

1. Каждый фикс — новый коммит с префиксом `fix(review):`.
2. После фикса — менять статус `OPEN` → `FIXED` в этом файле.
3. Если передумали — `WONTFIX` с причиной.
4. Обновлять таблицу прогресса.
5. Файл **удаляется** (или архивируется в `docs/archive/`), когда все P0+P1 закрыты и Фаза 2 стартовала.

---

## 🔴 P0 — РАСШИРЕНИЕ (пакет аудита 2, 2026-09-27)

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

### CR-055. `Application.cpp::Run()` — `static bool prevRMB` в member-функции

**Где:** `Application.cpp:1139-1140`

**Проблема:**
```
static bool  prevRMB = false;
static ImVec2 rmbStartPos(0, 0);
```

RULES п.20 нарушен. Сейчас работает (одно окно), но при создании второго `Application` — общий `prevRMB`. **Занести как B-18.**

**Фикс:** member-поля `myPrevRMB`, `myRmbStartPos`.

---

### CR-056. `Application.cpp::Run()` — `static double lastPreviewX/Y` — та же проблема

**Где:** `Application.cpp:1180`
**Фикс:** member-поля.

---

### CR-057. `Application.cpp::Run()` — мёртвые переменные

**Где:** `Application.cpp:1095-1097`

```
bool prevDelete = false;   // не используется
bool prevCtrlS  = false;   // не используется
bool prevCtrlO  = false;   // не используется
```

**Фикс:** удалить.

---

### CR-058. `Application.cpp::Run()` — магические числа

**Где:** `Application.cpp:1162, 1185`

```
if (dx*dx + dy*dy < 25.0f)             // 5px² — что такое 5?
if (dxp*dxp + dyp*dyp > 9.0)           // 3px² — комментарий есть
```

**Фикс:** `constexpr float kRmbClickToleranceSq = 25.0f;` + `kPreviewMoveToleranceSq = 9.0f;`.

---

### CR-059. `Application.cpp::Run()` — магия зон вьюпорта

**Где:** `Application.cpp:1146-1149`

```
bool inScene = io.MousePos.x > myTreeWidth + 10
            && io.MousePos.y > 165              // ← что такое 165?
            && io.MousePos.x < io.DisplaySize.x - 320   // ← 320?
            && io.MousePos.y < io.DisplaySize.y - 70;   // ← 70?
```

Все четыре — magic. `165` — высота ribbon+tabs (в UI_LAYOUT = 142). Снова расхождение с каноном.

**Фикс:** `constexpr` + актуализация UI_LAYOUT.

---

### CR-060. `Application.cpp::Run()` — `try {` без `catch` (проверить)

**Где:** `Application.cpp:1101`

Открывается `try {`, но в видимом фрагменте нет `catch`. Проверить ниже по файлу — есть ли.

**Если нет** — компилятор не пропустит. Если есть — **что там?** Глушится ли исключение? Логируется?

---

### CR-061. `Application.cpp::Run()` — паттерн `winW/winH/fbW/fbH` дублируется 3+ раз

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

## 🟠 P1 — РАСШИРЕНИЕ

### CR-032. `RibbonButton.cpp` — `inArrow` проверяет только `X`, не `Y`

**Где:** `RibbonButton.cpp:65`

```
bool inArrow = hasArrow && (mp.x >= min.x + w);
```

Если курсор **ниже** кнопки, но по X в зоне стрелки → `inArrow = true`. Hover подсветит стрелку, хотя мышь вне кнопки.

**Фикс:** добавить `mp.y >= min.y && mp.y <= max.y`.

---

### CR-033. `RibbonButton.cpp` — fallback-иконка — хардкод цвета

**Где:** `RibbonButton.cpp:105`

```
dl->AddRectFilled(p1, ..., IM_COL32(140, 140, 140, 160), 4.0f);
```

RULES п.30. В `ThemeTokens` — нет такого токена.

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

### CR-039. `RibbonGroup.cpp` — хардкод цветов

**Где:** `RibbonGroup.cpp:20, 33, 38-40`

```
bg = isLight ? IM_COL32(255, 255, 255, 60) : IM_COL32(255, 255, 255, 12);
isLight ? IM_COL32(160, 160, 160, 140) : IM_COL32(90, 100, 115, 180);
isLight ? IM_COL32(90, 90, 90, 230) : IM_COL32(180, 185, 195, 220);
```

RULES п.30.

---

### CR-040. `RibbonGroup.cpp` — `SameLine(0, 22)` magic

**Где:** `RibbonGroup.cpp:47`

---

### CR-041. `RibbonTabs.cpp` — `isLight` считается в цикле

**Где:** `RibbonTabs.cpp:22`
Вынести до цикла.

---

### CR-042. `RibbonTabs.cpp` — хардкод цветов вкладок

**Где:** `RibbonTabs.cpp:25-30`
`ImVec4(0.698f, 0.714f, 0.737f, 1.0f)` и `ImVec4(0.20f, 0.55f, 0.95f, 1.0f)` — RULES п.30.

---

### CR-043. `RibbonTabs.cpp` — magic числа SvgIconBtn

**Где:** `RibbonTabs.cpp:41`
`SvgIconBtn(icons, "ribbon_plus", "plus", 16, 32, 26, ...)` — `16, 32, 26`.

---

### CR-044. `RibbonTabs.cpp` — `SetTooltip("Подключить модуль")` без `Tr()`

**Где:** `RibbonTabs.cpp:45`
Локализация обойдена.

---

### CR-045. `SketchTab.cpp` — `static int projIdx = 0;`

**Где:** `SketchTab.cpp:36`
RULES п.20. Должно быть в `RibbonState`.

---

### CR-046. `SketchTab.cpp` — цвета стилей линий — хардкод

**Где:** `SketchTab.cpp:200-202`

```
{ "Основная",   "Основная линия",        LS_Normal,       IM_COL32(140, 51, 191, 255) },
{ "Вспомог.",   "Вспомогательная линия", LS_Construction, IM_COL32(115, 128, 166, 255) },
{ "Осевая",     "Осевая линия",          LS_Centerline,   IM_COL32(179, 102, 38, 255) },
```

RULES п.30. Цвета должны быть в `ThemeTokens`.

---

### CR-047. `SketchTab.cpp` — тихая деградация при `nullptr`

**Где:** `SketchTab.cpp:205, 219`

```
if (ctx.currentLineStyle) *ctx.currentLineStyle = items[i].style;
```

Если `nullptr` — подсветка работает, но выбор не сохраняется. Пользователь в недоумении. **Надо assert или лог.**

---

### CR-048. `SketchTab.cpp` — хардкод «неактивного» цвета

**Где:** `SketchTab.cpp:207`
`ImVec4(0.22f, 0.24f, 0.30f, 1.0f)` — RULES п.30.

---

### CR-054. `Annotate/Tools/Manage.cpp` — цвета текста не через тему

**Где:** все три файла
`ImGui::GetStyleColorVec4(ImGuiCol_Text)` — базовый ImGui, не токен проекта.

**Фикс:** после CR-011 автоматически уйдёт (там будет `GroupEnd` с `Tr`).

---

## 🟡 P2 — РАСШИРЕНИЕ

### CR-063. `RibbonButton.cpp` — `min/max` через два отдельных вызова

**Где:** `RibbonButton.cpp:59-60`
```
ImVec2 min = ImGui::GetItemRectMin();
ImVec2 max = ImGui::GetItemRectMax();
```
Норм, но `min`/`max` — плохие имена (конфликт с `std::min/max` в глазах читателя). **Фикс:** `rMin`, `rMax`.

---

### CR-064. `RibbonButton.cpp` — локальные `w`, `h` без `const`

**Где:** `RibbonButton.cpp:28-29`
```
float w = big ? bigW : smallW;
float h = big ? bigH : smallH;
```
Не меняются → `const float`.

---

### CR-065. `RibbonButton.cpp` — `(int)((big ? 32 : 24) * iconScale)` — C-style cast

**Где:** `RibbonButton.cpp:27`
**Фикс:** `static_cast<int>(...)`.

---

### CR-066. `RibbonButton.cpp` — `std::string label = Tr(opts[idx]);` — копия из временного

**Где:** `RibbonButton.cpp:118`
Каждый кадр — new `std::string`. Это тот самый **OPT-01** из REFACTOR_NOTES. **Фикс:** `const std::string& label = TrCached(opts[idx]);` с кэшем или reference на статическую таблицу.

---

### CR-067. `RibbonButton.cpp` — `drawBottom` берёт `std::vector<std::string>`

**Где:** `RibbonButton.cpp:134`
Каждый вызов — new vector. **Фикс:** `std::initializer_list<const char*>` или span.

---

### CR-068. `RibbonButton.cpp` — `auto [l1, l2] = splitMiddle(label);` создаёт копии строк

**Где:** `RibbonButton.cpp:145`
**Фикс:** вернуть `std::pair<string_view, string_view>`.

---

### CR-069. `RibbonButton.cpp` — `if (result == 2) ImGui::OpenPopup("##popup");`

**Где:** `RibbonButton.cpp:170`
Имя попапа **одинаковое для всех кнопок** (ID-скоуп спасает через `PushID(id)`, но всё равно неявно). **Фикс:** явное имя `"##ribbon_menu_popup"` + комментарий.

---

### CR-070. `ViewTab.cpp` — `Button(icons, "nav_pan", panOpts, 1, dummy, ...)` с `dummy`

**Где:** `ViewTab.cpp:22`
Согласовано с CR-050. Но `dummy` ещё и локальный `static` (см. CR-002 расширение).

---

### CR-071. `ViewTab.cpp` — `int dummy = 0;` в начале функции

**Где:** `ViewTab.cpp:17`
Хоть и не `static`, но `dummy` используется **только** для `pan`. **Фикс:** убрать, использовать один из `state.*` (например, `state.panToolIndex`).

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

### CR-073. `SketchTab.cpp` — `static const char* clearOpt[]` не используется

**Где:** `SketchTab.cpp:39`
Объявлено, нигде не используется. **Удалить.**

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

## 🟢 P3 — РАСШИРЕНИЕ

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

## 🔵 ОСОБЫЙ АУДИТ: RAM-рост (BUG-A3)

**Симптом (автор):** старт ~100 MB, через час работы ~150 MB и продолжает расти.

**Гипотезы:**

### RAM-H1. Кэш текстур иконок в `IconManager` 🔴

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

### RAM-H3. Кэш превью/миниатюр 🔴

**Подозрение:** `myThumbnailPaths` и `Thumbnails.cpp` — каждый проект держит PNG в памяти. Если много документов открыто/закрыто — накапливаются.

**Проверить:**
```
grep -an "myThumbnail\|CaptureCurrentThumbnail" src/app/Application.cpp | head -20
```

**Вероятность:** 🟠 средняя.

---

### RAM-H4. OCCT AIS_InteractiveObject — не освобождаются

**Подозрение:** каждый `AddBox`/`DisplaySketchLine` создаёт AIS-объект в `OcctView`. Если документ закрыт, но AIS не очищен — OCCT держит.

**Проверить:**
```
grep -an "myContext->Remove\|AIS_InteractiveContext::Remove\|myContext->Erase" src/occt/OcctView.cpp
```

**Вероятность:** 🔴 **высокая.**

---

### RAM-H5. `std::function` closure в `DrawRibbon` — накапливаются

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

**Подозрение:** в `myVisibility` — записи для всех фич. Если фичи удаляются, а записи остаются — медленный рост.

**Проверить:**
```
grep -an "myVisibility\[\|myVisibility.erase\|myVisibility.clear" src/app/Application.cpp
```

**Вероятность:** 🟠 средняя.

---

### RAM-H10. OCAF документы при закрытии не освобождаются

**Подозрение:** `CloseDoc` удаляет из `myDocuments`, но `Handle(TDocStd_Document)` может держаться где-то ещё (например, в `BreezeApplication`).

**Проверить:**
```
grep -an "mySession->Close\|mySession->NewDocument\|myDocuments.erase\|myDocuments.pop_back" src/app/Application.cpp
```

**Вероятность:** 🔴 **высокая.**

---

**План диагностики:**

1. **Замер базовый:** запустить, `tasklist | grep -i breezecad` каждые 30 сек — записать в файл.
2. **Сценарий А:** открыть-закрыть 10 документов — RAM?
3. **Сценарий Б:** нарисовать 100 линий в эскизе, выйти-войти — RAM?
4. **Сценарий В:** покрутить 3D 5 минут, вернуться в idle — RAM?
5. **Сценарий Г:** оставить в idle 30 минут без действий — RAM?

**Ожидаемые результаты:**
- Если растёт в idle → **утечка в draw-path** (RAM-H5, H6).
- Если растёт после сценария А → **OCAF/documents не освобождаются** (RAM-H10).
- Если растёт после сценария Б → **OCAF/AIS** (RAM-H4, H10).
- Если растёт после В → **OCCT V3d** (RAM-H4).

**Замер инструмент:** в ImGui уже есть FPS/RAM-телеметрия (`UpdateTelemetry`). Она читает WinAPI `GetProcessMemoryInfo` — точная.

**Отдельный файл:** `docs/RAM_AUDIT.md` — создать, когда начнём копать. Здесь только подозрения.

**Приоритет:** 🟠 **высокий.** Это нарушает философию MVP («RAM <500 МБ на сборке 200 деталей, idle 60 FPS»). Если idle растёт — MVP не получится.

**Порядок работ:**
1. Сначала CR-001 (убрать closures) — может уже дать −50% роста.
2. Потом RAM-H1 (кэш иконок) — если размер меняется.
3. Потом RAM-H4/H10 (OCCT/OCAF).
4. Диагностика сценариями.

---

## 📊 Прогресс (обновлён 2026-09-27)

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 11 | 0 |
| 🟠 P1 | 31 | 0 |
| 🟡 P2 | 22 | 0 |
| 🟢 P3 | 21 | 0 |
| 🔵 RAM | 10 гипотез | 0 |
| **Всего** | **85 + 10 RAM** | **0** |

**Основные категории:**
- **Функциональные баги:** CR-049, 050, 051, 053
- **RULES п.20 (static):** CR-002 (расширение на 5 файлов), 045, 055, 056
- **RULES п.30 (хардкод цветов):** CR-003, 033, 039, 042, 046, 048
- **Производительность:** CR-001, 010, 066, 067, 068
- **Стиль/архитектура:** остальные

---

## 🔴 P0 — ВОЛНА 2 (Application.cpp: конструктор, InitCommands, RefreshView)

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

**Где:** `Application.cpp:140`
RULES п.23: «Никаких хардкодов путей. `std::filesystem` или glfw-хелперы.»

**Фикс:** `std::filesystem::path exeDir = ...` + `exeDir / "assets" / "icons"`.

---

### CR-092. Рекурсивная `std::function` в `RefreshView` — heap на каждой итерации

**Где:** `Application.cpp:215`
```
std::function<void(const TDF_Label&)> walk = [&](const TDF_Label& l) {
    ...
    walk(it.Value());   // ← рекурсия через std::function = heap alloc
};
```

Для дерева из 1000 узлов — **1000 heap-аллокаций** на каждый `RefreshView`. `RefreshView` вызывается после каждой операции. **Плохо.**

**Фикс:** рекурсивная лямбда через `auto&& self` (Y-combinator) или free-функция.

---

### CR-093. `RefreshView`: `std::string key = LabelKey(l);` — heap на каждый узел

**Где:** `Application.cpp:224`
То же — 1000 heap-аллокаций на обход.

**Фикс:** `myVisibility` по `TDF_Label` напрямую или по `uint64_t` hash.

---

### CR-094. `RefreshView`: `myOcctView->ClearAllShapes()` + полный обход каждый раз

**Где:** `Application.cpp:205, 242`
После каждой операции — **полная переотрисовка** всего дерева. Для 1000 фич — O(N) на операцию → O(N²) на сессию.

**Фикс:** инкрементальный diff — обновлять только изменённые узлы.

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

### CR-097. `InitCommands`: `std::cout` — BUG-A4 (кириллица)

**Где:** `Application.cpp:1068`
`std::cout << "InitCommands: ..."` — если когда-нибудь добавишь кириллицу в лог, получишь CP866-кракозябры (BUG-A4).

**Фикс:** единый logger или `setlocale`.

---

### CR-098. `UpdateTelemetry`: `PROCESS_MEMORY_COUNTERS pmc;` — не инициализирован

**Где:** `Application.cpp:1077`
```
PROCESS_MEMORY_COUNTERS pmc;   // ← мусор
if (GetProcessMemoryInfo(...)) { myLastRAMMB = ...; } else { myLastRAMMB = 0.0; }
```
Формально ок (myLastRAMMB перезапишется), но паттерн плохой. **Фикс:** `= {};`.

---

### CR-099. `UpdateTelemetry`: `(1024.0 * 1024.0)` — magic

**Фикс:** `constexpr double kBytesPerMB = 1024.0 * 1024.0;`.

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

### CR-103. `InitCommands`: команды не локализованы через ключи

**Где:** `Application.cpp:1040-1066`
```
add("file.new", "Создать", "Ctrl+N", ...);   // ← русский хардкод
```
`name` идёт хардкодом. Локализация обходится. При переключении на EN — команды останутся на русском.

**Фикс:** `add("file.new", "cmd.file.new", ...)` + `Tr()`.

---

### CR-104. `RefreshView`: `LabelKey(l)` возвращает `std::string`

**Где:** `Application.cpp:224`
Уже в CR-093. Плюс: что делает `LabelKey`? Ищет по имени или по `Entry()`? Если по имени — там кириллица (BUG-A5, `TCollection_AsciiString`).

---

### CR-105. `RefreshView`: `myOcctView->Redraw()` в конце — синхронно

**Где:** `Application.cpp:241`
`RefreshView` вызывается часто (после каждой операции). `Redraw()` — блокирующий. **Фикс:** отложить до следующего кадра, вызывать в `Run()`.

---

## 🟠 P1 — ВОЛНА 2

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

### CR-110. `~Application`: `myIcons` не освобождает GL-текстуры

**Где:** `Application.cpp:199-203`
`IconManager` держит `GLuint` текстуры. Деструктор их не удаляет. **Утечка GPU RAM** при каждом создании/уничтожении Application (например, в тестах).

**Связано с RAM-H1.**

---

## 📊 Прогресс (обновлён 2026-09-27, после Волны 2)

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 22 | 0 |
| 🟠 P1 | 38 | 0 |
| 🟡 P2 | 22 | 0 |
| 🟢 P3 | 21 | 0 |
| 🔵 RAM-гипотезы | 10 | 0 |
| **Всего** | **113 записей** | **0** |

**Покрытие аудита:**
- src/ui/ribbon/ — 100% ✅
- Application.cpp — ~60% (осталось OnSave/OnOpen/OnExport, DocumentManager, OnSketchClick, EnterSketch/ExitSketch, DrawViewCube)
- Application.h — 100% ✅
- Остальные 30+ модулей — 0% ⚪
- CMakeLists.txt — 0% ⚪

**Следующие волны:**
- **Волна 3:** остаток Application.cpp (DocumentManager, FileOps, скетч-логика).
- **Волна 4:** CMakeLists.txt — флаги.
- **Волна 5:** doc/BreezeDocument.cpp + occt/OcctView.cpp + app/IconManager.cpp — RAM-фронт.
- **Волна 6:** ui/theme/ + ui/widgets/ — RULES п.30.
- **Волна 7:** ui/qat/, ui/status/, ui/home/, ui/panels/, ui/dialogs/, ui/viewcube/.

---

## 🔴 P0 — ВОЛНА 3 (DocumentManager / FileOps / Undo-Redo)

### CR-111. `SwitchToDoc` — `*myOcctView` без проверки на nullptr

**Где:** `Application.cpp:296`
```
UI::Home::CaptureCurrentThumbnail(*myOcctView, myActiveDocIndex, myThumbnailPaths);
```
Если `myOcctView == nullptr` (не инициализировался, был reset) — **UB, краш**.

**Фикс:** `if (myOcctView) UI::Home::CaptureCurrentThumbnail(...);`

**Оценка:** 1 минута.

---

### CR-112. `OnSave` не обновляет `myProjectPaths[myActiveDocIndex]`

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

### CR-113. `OnSave` не обновляет `myProjectNames[myActiveDocIndex]`

**Где:** `Application.cpp:363-378`

По аналогии с CR-112. Пользователь сохранил `Project1` как `Деталь_фланца.cbf` — имя проекта остаётся `Project1`. **UX-баг.**

**Фикс:** `myProjectNames[myActiveDocIndex] = <имя из path>;` (как в `AddDocFromFile`).

**Оценка:** 5 минут.

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

### CR-114. `myVisibility.clear()` при switch/close — теряется состояние видимости

**Где:** `Application.cpp:305, 352, 466`

Пользователь:
1. В документе A скрыл ось X (`myVisibility["..."] = false`).
2. Переключился в документ B (`SwitchToDoc` → `myVisibility.clear()`).
3. Вернулся в A.
4. **Ось X снова видна.**

**Фикс:** `myVisibility` должен быть **per-document**, а не глобальный. Хранить в `BreezeDocument` или в `std::map<int, map<string, bool>>`.

**Оценка:** 30 минут.

---

### CR-115. `CloseDoc` не сохраняет превью перед закрытием

**Где:** `Application.cpp:318-362`

`SwitchToDoc` вызывает `CaptureCurrentThumbnail`, но `CloseDoc` — **нет**. Если пользователь закрывает документ, превью для него не сохраняется → в Home Screen карточка без обновлённого превью.

**Фикс:** вызвать `CaptureCurrentThumbnail` в начале `CloseDoc`.

**Оценка:** 5 минут.

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

### CR-122. `AddDocFromFile` / `OnOpen` — нет проверки на дубликат

**Где:** `Application.cpp:269, 452`

Пользователь открывает `foo.cbf` дважды → два `BreezeDocument` с одним файлом. При сохранении один перезапишет другой.

**Фикс:** проверить `myProjectPaths` на наличие `path`, если есть — `SwitchToDoc`.

**Оценка:** 10 минут.

---

### CR-124. `static bool RibbonButton` — хардкод цветов (B-12)

**Где:** `Application.cpp:531-548`

```
if (active) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 90, 140, 255));
} else {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(120, 30, 30, 255));
    ...
}
```

Это тот самый **B-12 debug-helper, KEEP**. Но цвета — **хардкод** (RULES п.30).

**Решение:** оставить функцию, но цвета — из темы. Либо пометить как debug-only, не участвующий в продакшене.

**Оценка:** 10 минут.

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

## 🟠 P1 — ВОЛНА 3

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

### CR-123. `std::cout` / `std::cerr` — BUG-A4

**Где:** 15+ мест в этом блоке.
Кириллица → CP866-кракозябры в MSYS2.

**Фикс:** единый логгер или `setlocale`.

---

## 🟡 P2 — ВОЛНА 3

### CR-127. `OnNewDocument` — тонкая обёртка, но дублирует логику `OnOpen` и `AddDocFromFile`

**Где:** `Application.cpp:244-248, 452-468`
```
// OnNewDocument
CreateNewDoc(); RefreshView(); FitAll();

// OnOpen
AddDocFromFile(...); AddRecentFile(...); myVisibility.clear(); RefreshView(); FitAll();
```

Паттерн «создать → refresh → fit» повторяется. **Фикс:** `ActivateDocument()`.

---

### CR-128. `AddDocFromFile` дублирует логику `CreateNewDoc`

Уже **B-03** в `REFACTOR_NOTES`. Подтверждается.

**Фикс:** единый `RegisterDocument(name, path, doc)`.

---

### CR-129. Параллельные векторы `myDocuments` / `myProjectNames` / `myProjectPaths`

Уже **B-03** + **C-04**. Расширение: заменить на `struct DocEntry { unique_ptr<BreezeDocument> doc; string name; string path; }` + `vector<DocEntry>`.

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

## 📊 Прогресс (после Волны 3)

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 29 | 0 |
| 🟠 P1 | 47 | 0 |
| 🟡 P2 | 34 | 0 |
| 🟢 P3 | 21 | 0 |
| 🔵 RAM-гипотезы | 10 | 0 |
| **Всего** | **131 + 10 RAM** | **0** |

**Покрытие:**
- src/ui/ribbon/ — 100% ✅
- **Application.cpp — ~85%** (осталось: OnSketchClick 578-813, EnterSketch/ExitSketch 814-930, DrawPreviewInfo, DrawViewCube, RequestDelete)
- Application.h — 100% ✅
- Остальное — 0% ⚪

---

## 🔴 P0 — ВОЛНА 4 (sketch-логика: OnSketchClick, RefreshSketchLines, EnterSketch)

### CR-133. `std::cout` в sketch-логике — hot-path

**Где:** `Application.cpp:712, 727, 737, 748, 754, 764, 766, 771, 775, 778` (и в `Run()` тоже)

На **каждый клик** в эскизе — 5-7 вызовов `std::cout << ...`. На слабом железе (ARM, iGPU) `std::cout` **синхронный**, каждый flush — syscall.

**Дополнительно:** BUG-A4 — кириллица, но здесь ASCII, поэтому не проявляется.

**Фикс:**
1. Убрать `std::cout` из production-кода (заменить на `TRACE`).
2. Или `#ifdef BREEZECAD_DEBUG` — только в debug-сборке.

**Оценка:** 20 минут.

---

### CR-137. `SnapToKnownPoint` — `std::vector<gp_Pnt>` на каждый вызов

**Где:** `Application.cpp:658`

```
std::vector<gp_Pnt> candidates;   // heap alloc, до 2*N entries
candidates.push_back(gp_Pnt(0, 0, 0));
...  // по 2 точки на каждую линию эскиза
```

Вызывается из `OnSketchClick` **на каждый клик**, а в `Run()` — **на каждый пиксель движения мыши** (>3px сдвиг).

При 100 линиях в эскизе — 200 heap-аллокаций на кадр движения. **Фикс:**
1. Два прохода: сначала find best, потом без вектора.
2. Или резервировать `vector` в `myCandidateBuffer` (member) и переиспользовать.

**Оценка:** 20 минут.

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

## 🟠 P1 — ВОЛНА 4

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

### CR-138. `SnapToKnownPoint` — `threshold * threshold` вычисляется 1 раз, ок, но `dx*dx+dy*dy+dz*dz` можно через `BRep_Tool::Pnt` или OCCT helper

Мелочь.

---

### CR-139. `SnapToKnownPoint` — `doc` параметр не используется для валидации `sketchLabel`

**Где:** `Application.cpp:642`

```
if (doc.IsNull() || sketchLabel.IsNull()) return false;
```

Проверяет оба — ок. Но `doc` далее нигде не используется. Убрать параметр?

---

### CR-140. `SnapToKnownPoint` — `bestDist` без нормализации на `threshold`

```
double bestDist = threshold * threshold;
```
Если `threshold = 3.0`, `bestDist = 9.0`. Если `threshold = 0.1`, `bestDist = 0.01`. Пороговое сравнение — ок, но неясно **почему** именно так.

**Фикс:** `const double thresholdSq = threshold * threshold;` — сразу к квадрату, комментарий.

---

### CR-141. `OnSketchClick` — `myDocument->GetDoc()` в hot-path

**Где:** `Application.cpp:723`

`GetDoc()` — возвращает `Handle`, refcount inc/dec на каждый вызов. **Фикс:** проверить сигнатуру, возможно `const Handle&`.

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

## 🟡 P2 — ВОЛНА 4

### CR-161. `IsPointOnExistingVertex` / `SnapToKnownPoint` — дублируют логику обхода `TDF_ChildIterator`

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

## 📊 Прогресс (после Волны 4)

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 38 | 0 |
| 🟠 P1 | 53 | 0 |
| 🟡 P2 | 42 | 0 |
| 🟢 P3 | 21 | 0 |
| 🔵 RAM-гипотезы | 10 | 0 |
| **Всего** | **154 + 10 RAM** | **0** |

**Покрытие:**
- src/ui/ribbon/ — 100% ✅
- **Application.cpp — ~95%** (осталось: `ExitSketch`, `ApplyExactLine`, `DrawPreviewInfo`, `DrawViewCube`, `RequestDelete`)
- Application.h — 100% ✅
- Остальное — 0% ⚪

**Накоплено на разбор** (обновить покрытие):

Приложение уже растёт до 2000 строк. Скоро надо будет дробить.

---

## 🔴 P0 — ВОЛНА 5 (ExitSketch, ApplyExactLine, DrawPreviewInfo, DrawViewCube, RequestDelete)

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

### CR-167. `DrawPreviewInfo` — 7 хардкод-строк на английском

**Где:** `Application.cpp:934, 939, 953, 956, 971, 975, 978, 979`

```
ImGui::TextColored(..., "Length: %.2f mm", myPreviewLength);    // ← "Length:"
ImGui::Text("Exact input:");                                     // ← "Exact input:"
ImGui::InputText("Length", ...);                                 // ← "Length"
ImGui::InputText("Angle", ...);                                  // ← "Angle"
ImGui::Button("Apply", ...);                                     // ← "Apply"
ImGui::Button("Cancel", ...);                                    // ← "Cancel"
ImGui::TextDisabled("Tab - focus Length");                       // ← англ.
ImGui::TextDisabled("Enter - apply");                            // ← англ.
```

Локализация **полностью обойдена**. В русском UI пользователь видит «Exact input:» и «Tab - focus Length».

**Фикс:** `Tr("preview.length")`, `Tr("preview.exact_input")`, `Tr("preview.tab_hint")`, `Tr("preview.enter_hint")`, `Tr("btn.apply")`, `Tr("btn.cancel")` и т.д.

**Оценка:** 20 минут + 8 новых ключей в `Localization.cpp`.

---

### CR-168. `DrawPreviewInfo` — хардкод цветов (RULES п.30)

**Где:** `Application.cpp:934-935`

```
ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), ...);   // жёлтый
ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), ...);   // голубой
```

Прямое нарушение RULES п.30. Ни в светлой, ни в тёмной теме эти цвета не согласованы с палитрой.

**Фикс:** `myTheme.tokens.previewLengthColor`, `.previewAngleColor` или взять из существующих токенов (warning, info).

**Оценка:** 10 минут.

---

### CR-169. `DrawPreviewInfo` — `static bool focusLength` в member-функции

**Где:** `Application.cpp:944`

```
static bool focusLength = false;   // ← RULES п.20
```

**Это B-04 в REFACTOR_NOTES.** Сейчас функционально работает (одно окно preview), но если появится второй preview или функция вынесется в модуль — сломается.

**Фикс:** member-поле `myFocusLengthInput`.

**Оценка:** 3 минуты.

---

### CR-170. `DrawPreviewInfo` — магические числа позиции/размера окна

**Где:** `Application.cpp:925, 927`

```
ImVec2 pos = ImVec2(io.DisplaySize.x - 280, 60);   // ← 280, 60
ImGui::SetNextWindowSize(ImVec2(260, 0), ...);      // ← 260
```

**Фикс:** `constexpr float kPreviewMarginRight = 280.0f;` и т.д.

**Оценка:** 5 минут.

---

### CR-171. `DrawPreviewInfo` — `myOcctView->ClearPreviewLine()` без null check

**Где:** `Application.cpp:974`

```
if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    myHasSketchStart = false;
    myOcctView->ClearPreviewLine();   // ← если myOcctView = nullptr → краш
}
```

В начале функции проверяется `myActiveSketchLabel` и `myHasSketchStart`, но **не** `myOcctView`. Учитывая, что функция может вызываться в разных состояниях (после краха Init, например), — потенциальный краш.

**Фикс:** `if (myOcctView) myOcctView->ClearPreviewLine();`

**Оценка:** 1 минута.

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

### CR-174. `EnterSketch` / `ExitSketch` / `ApplyExactLine` — `std::cout` в hot-path

**Где:** `Application.cpp:848, 865, 879, 907, 909`

```
std::cout << "Application: entered sketch mode\n";
std::cout << "Application: exited sketch mode\n";
std::cout << "ApplyExactLine: length too small\n";
std::cout << "ApplyExactLine: line added, continuing\n";
std::cout << "ApplyExactLine: loop closed\n";
```

Связано с CR-133. `ExitSketch` вызывается на **каждый Esc**, `ApplyExactLine` — на **каждый точный ввод**.

**Фикс:** все `std::cout` → `TRACE(...)` (no-op по умолчанию).

**Оценка:** входит в CR-133.

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

## 🟠 P1 — ВОЛНА 5

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

**Где:** `Application.cpp:927, 931`

```
ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);   // фикс ширина
...
ImGuiWindowFlags_AlwaysAutoResize                              // авто размер
```

Два взаимоисключающих флага. Скорее всего работает, но непредсказуемо. **Фикс:** выбрать одно.

---

### CR-182. `DrawPreviewInfo` — `Tab` перехватывает фокус без проверки контекста

**Где:** `Application.cpp:944-947`

```
if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !ImGui::GetIO().WantTextInput) {
    focusLength = true;
}
```

`Tab` в ImGui используется для навигации. Перехват может сломать UI-навигацию в других виджетах. **Проверить.**

---

## 🟡 P2 — ВОЛНА 5

### CR-183. `DrawViewCube` — `myRightPanelExpanded ? 110.0f : 55.0f` — магия

Два числа для смещения. Вынести в константы.

### CR-184. `DrawViewCube` — `Context` создаётся каждый кадр

Если `Context` содержит `IconManager&` или другие тяжёлые — heap-аллокации. Проверить определение `Context`.

### CR-185. `DrawSketchOverlay` — пустая функция (B-15, KEEP)

Оставить, но добавить `// TODO (Фаза 2): overlay-подсказки эскиза`.

### CR-186. `RequestDelete` — проверка `myDeleteConfirmState.skipConfirm` — где пользователь настраивает?

Кто ставит `skipConfirm = true`? Настройка? Проверить.

### CR-187. `ApplyExactLine` — `myInputActive = false` в конце, но не сбрасывается при ранних `return`

При `length < 1e-6` — выход **без** `myInputActive = false`. Флаг остаётся `true`. **Фикс:** установить в начале или RAII-guard.

### CR-188. `EnterSketch` — не сбрасывает `myLengthBuffer`/`myAngleBuffer`

Пользователь вышел из эскиза с `length = "500"`. Вошёл в новый эскиз. Поле показывает `500`. **UX-фикс:** сбрасывать при входе.

### CR-189. `ExitSketch` — не сбрасывает `myInputActive`

Связано с CR-188.

### CR-190. `ApplyExactLine` — `catch(...)` без сброса `myInputActive`

```
} catch (...) {
    std::cerr << "ApplyExactLine exception\n";
}
```

`myInputActive` остаётся `true`. **Фикс:** сброс в catch.

---

## 📊 Прогресс (после Волны 5)

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 49 | 0 |
| 🟠 P1 | 58 | 0 |
| 🟡 P2 | 50 | 0 |
| 🟢 P3 | 21 | 0 |
| 🔵 RAM-гипотезы | 10 | 0 |
| **Всего** | **188 + 10 RAM** | **0** |

**Покрытие:**
- src/ui/ribbon/ — 100% ✅
- **Application.cpp — ~99%** (RequestDelete — нужен полный код)
- Application.h — 100% ✅
- **CMakeLists.txt — 0%** ⚪ (следующий)

**Файл на 3000 строк.** После RequestDelete + CMake — **разбиваем на docs/review/**.

---

## 🔴 P0 — ВОЛНА 6 (RequestDelete + CMakeLists.txt)

### CR-191. `RequestDelete` — `<unnamed>` не локализовано

**Где:** `Application.cpp:1010`
```
std::string name = "<unnamed>";
```

**Фикс:** `Tr("misc.unnamed")`.

---

### CR-192. `RequestDelete` — русский хардкод в `snprintf`

**Где:** `Application.cpp:1023, 1027`
```
"Удалить «%s»?\n\nВместе с ним будут удалены %d вложенных элементов."
"Удалить «%s»?"
```

Формат русский — хорошо. Но если формат шаблона меняется при локализации (порядок аргументов в EN другой), `snprintf` сломается. **Фикс:** `Tr` с параметрами или `{name}` шаблон.

---

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

### CR-195. `RequestDelete` — `RefreshView()` + `FitAll()` дублирует паттерн

Связано с CR-127. Паттерн «после операции → RefreshView + FitAll» повторяется 6+ раз.

**Фикс:** `Application::AfterModelChange()`.

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

## 🟠 P1 — ВОЛНА 6

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

## 📊 Прогресс (после Волны 6)

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 61 | 0 |
| 🟠 P1 | 63 | 0 |
| 🟡 P2 | 50 | 0 |
| 🟢 P3 | 21 | 0 |
| 🔵 RAM-гипотезы | 10 | 0 |
| **Всего** | **205 + 10 RAM** | **0** |

**Покрытие:**
- src/ui/ribbon/ — 100% ✅
- src/app/Application.cpp — 100% ✅
- src/app/Application.h — 100% ✅
- CMakeLists.txt — 100% ✅
- Остальные модули (~30) — 0% ⚪

**Следующая волна:** модули, помеченные как **RAM-критичные** (doc/BreezeDocument, occt/OcctView, app/IconManager), потом RULES п.30 (ui/theme), потом остальное.

**Файл ~3300 строк.** После Волны 7-8 — разбиваем.


---

## 🔴 P0 — ВОЛНА 7: RAM-ФРОНТ (IconManager)

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

### CR-219. `IconManager` — хардкод путей (`assets/icons/`, `../assets/icons/`)

**Где:** `IconManager.cpp:217-222`

```
std::vector<std::string> candidates = {
    myIconDir + "/" + realName + ".svg",
    "assets/icons/" + realName + ".svg",
    "../assets/icons/" + realName + ".svg",
    "../../assets/icons/" + realName + ".svg",
};
```

RULES п.23: «Никаких хардкодов путей.» Здесь четыре хардкода подряд.

**Фикс:** `std::filesystem::path exeDir = ...` + `exeDir / "assets" / "icons"`.

**Оценка:** 15 минут.

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

### CR-224. `IconManager` — `myCache` без очистки — растёт от смены размера

**Где:** `IconManager.cpp:205-209`

```
std::string key = name + "#" + std::to_string(size);
auto it = myCache.find(key);
if (it != myCache.end()) return it->second;
...
myCache[key] = tex;
```

**Кэш никогда не чистится.** Если пользователь:
- Меняет масштаб UI → меняются размеры иконок → новые ключи → новые текстуры.
- Много разных размеров (`Get("icon", 16)`, `Get("icon", 24)`, `Get("icon", 32)`, `Get("icon", 48)`) → 4 текстуры на одну иконку.

**При 105 иконках × 4 размера = 420 текстур × 64×64×4 = 6.9 МБ GPU** — накопительно.

**Связано с RAM-H1.** **Фикс:** LRU-кэш с лимитом (например, 256 текстур).

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

## 🟠 P1 — ВОЛНА 7

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

## 📊 Прогресс (после Волны 7)

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 64 | 0 |
| 🟠 P1 | 68 | 0 |
| 🟡 P2 | 50 | 0 |
| 🟢 P3 | 21 | 0 |
| 🔵 RAM-гипотезы | 10 (H1 подтверждена, H2 снята) | 0 |
| **Всего** | **220 + 10 RAM** | **0** |

**Покрытие:**
- src/ui/ribbon/ — 100% ✅
- src/app/Application.cpp — 100% ✅
- src/app/Application.h — 100% ✅
- **src/app/IconManager.{h,cpp} — 100%** ✅
- CMakeLists.txt — 100% ✅
- Остальное — 0% ⚪

**RAM-фронт: 1 из 3 файлов разобран.** Следующий — `BreezeDocument` (RAM-H4/H10), потом `OcctView` (RAM-H4).


---

## 🔴 P0 — ВОЛНА 7: RAM-ФРОНТ (BreezeDocument)

### CR-232. `AddCylinderFeature` и `AddSketchFeature` — БЕЗ транзакции OCAF

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

### CR-233. `AddSketchLine` — транзакция открыта при раннем `return`

**Где:** `BreezeDocument.cpp:266-289`

```
StartTransaction();

int tag = 1;
while (!sketchLabel.FindChild(tag, false).IsNull()) tag++;

TDF_Label lineLabel = sketchLabel.FindChild(tag, true);
TDataStd_Integer::Set(lineLabel, FT_SketchLine);

TDF_Label styleLabel = lineLabel.FindChild(100, true);
TDataStd_Integer::Set(styleLabel, static_cast<int>(style));

TopoDS_Shape edge = BRepBuilderAPI_MakeEdge(p1, p2).Shape();
if (edge.IsNull()) {
    std::cerr << "AddSketchLine: edge is null\n";
    CommitTransaction();   // ← вызывается! но edge не создан, зачем коммит?
    return TDF_Label();
}
TNaming_Builder(lineLabel).Generated(edge);
CommitTransaction();
```

**Проблемы:**
1. При `edge.IsNull()` → `CommitTransaction()` **завершает** транзакцию, хотя фича не создана. Мусор в истории undo.
2. При **исключении** в `TNaming_Builder` → catch → **возврат** без `CommitTransaction` / `AbortCommand`. `OpenCommand` остаётся открытым.
3. Следующий вызов `AddBoxFeature` → `StartTransaction` → `OpenCommand` **второй раз** на той же транзакции → тихое наложение.

**Фикс:**
- `AbortCommand` при ошибке.
- `try/catch` со сбросом транзакции.

**Оценка:** 20 минут.

---

### CR-234. `AddBoxFeature` — транзакция не закрывается при исключении

**Где:** `BreezeDocument.cpp:187-206`

```
StartTransaction();

TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);
TDataStd_Name::Set(label, Utf8ToExt(name));
TDataStd_Integer::Set(label, FT_Box);

gp_Pnt corner(px, py, pz);
TopoDS_Shape box = BRepPrimAPI_MakeBox(corner, dx, dy, dz).Shape();  // ← может бросить

TNaming_Builder(label).Generated(box);
CommitTransaction();  // ← НЕ выполнится при исключении
```

Если `dx/dy/dz < 0` — OCCT бросает `Standard_DomainError`. Транзакция остаётся открытой.

**Тот же паттерн в `RenameFeature` и `RemoveFeature`** — но там меньше шансов на исключение.

**Фикс:** RAII-guard для транзакции:
```
class OcafTransaction {
    Handle(TDocStd_Document) myDoc;
    bool myCommitted = false;
public:
    OcafTransaction(Handle(TDocStd_Document) d) : myDoc(d) { myDoc->OpenCommand(); }
    ~OcafTransaction() { if (!myCommitted) myDoc->AbortCommand(); }
    void Commit() { myDoc->CommitCommand(); myCommitted = true; }
};
```

**Оценка:** 40 минут.

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

### CR-237. `~BreezeDocument` = default — RAM-H10-кандидат

**Где:** `BreezeDocument.h:51`

**Проблема:** `~BreezeDocument() = default;` — `myDoc` (Handle) декрементится. Но:
- `BreezeApplication::Instance().CreateDocument()` создаёт документ в **сессии** OCAF.
- Сессия `TDocStd_Application` **держит** документ, пока `CloseDocument` не вызван.
- Если `BreezeDocument` уничтожен **без** `CloseDocument` — OCAF-документ **остаётся в сессии**.

**Что это значит для RAM:** каждый `CloseDoc` в Application вызывается корректно (`BreezeApplication::CloseDocument`), но:
- `BreezeDocument` в конструкторе **создаёт** документ через `BreezeApplication::Instance().CreateDocument()`.
- Деструктор **не закрывает** документ.
- Если `BreezeDocument` создан, но не закрыт через сессию — утечка.

**Проверить:** `BreezeApplication::CreateDocument` и `CloseDocument`.

**Фикс:** в `~BreezeDocument` — если `myDoc` не Null и не был передан извне — закрыть через сессию.

**Оценка:** 20 минут (после проверки BreezeApplication).

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

### CR-241. `GetAllShapes` — возвращает `vector<TopoDS_Shape>` по значению

**Где:** `BreezeDocument.cpp:400-406`

```
std::vector<TopoDS_Shape> BreezeDocument::GetAllShapes() const {
    std::vector<TopoDS_Shape> shapes;
    if (myDoc.IsNull()) return shapes;
    CollectShapes(MainLabel(), shapes);   // O(N) элементов
    return shapes;                        // ← копия / move
}
```

Вызывается в `OnExportSTEP`, `OnExportSTL`. Для 1000 фич — 1000 `TopoDS_Shape` копируется (каждый — Handle с инкрементом refcount).

**Фикс:** RVO/NRVO сработает, но не для всех компиляторов. `std::move` явно. Или функция `CollectShapes(vector&)` как API — вызывающий сам создаёт вектор.

**Оценка:** 10 минут.

---

### CR-242. `ClearSketch` — стилевые метки не очищаются

**Где:** `BreezeDocument.cpp:335-350`

```
std::vector<TDF_Label> toRemove;
TDF_ChildIterator it(sketchLabel, false);
for (; it.More(); it.Next()) toRemove.push_back(it.Value());
for (auto& l : toRemove) l.ForgetAllAttributes(true);
```

Обходим **прямых** детей sketch. Но каждый `lineLabel` имеет **style-ребёнка** (tag 100). Он не забывается.

**Связано с CR-236.** **Фикс:** рекурсивный Forget.

---

### CR-243. `RemoveLastSketchLine` — style остаётся

То же, что CR-242. После удаления последней линии — style-метка (tag 100) остаётся в OCAF. Через `AddSketchLine` при совпадении tag 100 — перезапишется (ок), но между удалением и созданием — «висячая» метка.

---

### CR-244. `BreezeDocument` — `std::cout` в 12+ местах

Связано с CR-133. **Фикс:** переход на `TRACE` / убрать.

---

### CR-245. `BreezeDocument.cpp` — неиспользуемые include

**Где:** начало файла

```
#include <TCollection_AsciiString.hxx>   // ← не используется (BUG-A5 — в LabelName)
#include <gp_Lin.hxx>                     // ← не используется
```

**Фикс:** удалить.

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

### CR-250. `CollectShapes` — без защиты от циклов

**Где:** `BreezeDocument.cpp:390-398`

```
void BreezeDocument::CollectShapes(const TDF_Label& label, std::vector<TopoDS_Shape>& out) const {
    ...
    TDF_ChildIterator it(label, false);
    for (; it.More(); it.Next()) CollectShapes(it.Value(), out);   // рекурсия
}
```

OCAF-дерево не должно иметь циклов, но если баг приведёт к таковому — **stack overflow**. Защита: max-depth.

**Оценка:** 5 минут.

---

## 🔵 RAM — обновление гипотез

### RAM-H10 [ПОДТВЕРЖДЕНО косвенно]

**`~BreezeDocument` = default** — не закрывает документ через OCAF-сессию. Если `BreezeApplication` не отслеживает и не закрывает — `TDocStd_Document` остаётся в сессии до конца процесса.

**Требует проверки:** `BreezeApplication::CloseDocument` — вызывается ли на каждый `~BreezeDocument`?

**Проверить в:** `src/doc/BreezeApplication.cpp` (следующий файл).

---

### RAM-H4 [НЕ ПОДТВЕРЖДЕНО здесь]

`BreezeDocument` **не держит GL-ресурсов** — только OCAF + TopoDS_Shape. `RAM-H4` — про `OcctView` (следующий файл).

---

## 📊 Прогресс (после Волны 8 — BreezeDocument)

| Severity | Открыто | Закрыто |
|---|---|---|
| 🔴 P0 | 71 | 0 |
| 🟠 P1 | 76 | 0 |
| 🟡 P2 | 55 | 0 |
| 🟢 P3 | 21 | 0 |
| 🔵 RAM-гипотезы | 10 (H1 ✅, H2 снята, H10 подтверждена косвенно) | 0 |
| **Всего** | **233 + 10 RAM** | **0** |

**Покрытие:**
- ✅ Application.{h,cpp}, IconManager.{h,cpp}, BreezeDocument.{h,cpp}, CMakeLists.txt, ui/ribbon/ (всё)
- ⏳ **OcctView** (последний критичный файл RAM)
- ⚪ Остальные ~30 модулей

**Следующий — OcctView (1098 строк)** — самый большой, RAM-H4, BUG-A2.


---

## 🔴 P0 — ВОЛНА 7: RAM-ФРОНТ (OcctView, часть 1)

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

- `<AIS_Line.hxx>` — строки 5 и 9
- `<AIS_SelectionModesConcurrency.hxx>` — 16 и 17
- `<Geom_CartesianPoint.hxx>` — 7 и 13
- `<gp_Lin.hxx>` — 24 и 25

**Фикс:** убрать дубли.

**Оценка:** 2 минуты.

---

### CR-253. `~OcctView() = default` — потенциальная GPU-утечка

**Где:** `OcctView.cpp:35`

Все handle'ы освободятся автоматически. **Но** `myContext` не вызывает `Remove()` для отображаемых объектов. OCCT не гарантирует очистку GL-ресурсов без явного `RemoveAll`.

**Фикс:**
```
OcctView::~OcctView() {
    if (!myContext.IsNull()) {
        myContext->RemoveAll(false);
        myContext->UpdateCurrentViewer();
    }
}
```

**Оценка:** 15 минут.

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

## 🟠 P1 — ВОЛНА 8 (OcctView)

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

### CR-279. `OcctView::DisplayShape` — магические цвета `0.761, 0.541, 0.137` vs `IM_COL32`

**Где:** `OcctView.cpp:168-170`

```
Quantity_Color faceFill(0.761, 0.541, 0.137, Quantity_TOC_RGB);  // #c28a23
Quantity_Color faceEdge(0.588, 0.357, 0.125, Quantity_TOC_RGB);  // #965b20
```

RULES п.30: цвета **от темы**. Здесь хардкод, дублируется дважды (в двух блоках).

**Фикс:** `myTheme.tokens.planeFaceColor` и т.д.

**Оценка:** 20 минут.

---

### CR-280. `OcctView.cpp` — `std::cout` в `EnterSketchMode` / `ExitSketchMode`

**Где:** `OcctView.cpp:369` и другие

Связано с CR-133. **Фикс:** TRACE.

---

---

## 🔴 P0 — ВОЛНА 8 (OcctView часть 2: ExitSketchMode, ScreenToPlane, Preview/Marker)

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

### CR-285. `ShowPreviewLine` — magic `1e-6` для минимальной длины

**Где:** `OcctView.cpp:451`

```
if (dx*dx + dy*dy + dz*dz < 1e-6) return;
```

**Фикс:** `constexpr double kMinPreviewLenSq = 1e-6;`.

**Оценка:** 3 минуты.

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

### CR-292. `DisplaySketchLine` — цвет хардкод (RULES п.30)

**Где:** `OcctView.cpp:626-644`

```
case 1: color = Quantity_Color(0.55, 0.55, 0.75, ...);  // Construction
case 2: color = Quantity_Color(0.9, 0.45, 0.15, ...);   // Centerline
default: color = Quantity_Color(0.75, 0.2, 1.0, ...);   // Normal
```

Плюс эти же цвета **дублируются** в `SketchTab.cpp` (CR-046). Два источника правды → разъедутся.

**Фикс:** единый `ThemeTokens::lineStyleColor(style)`.

**Оценка:** 30 минут.

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

### CR-295. `OcctView.cpp` — `#include <gp_Lin.hxx>` дважды, `<AIS_Line.hxx>` дважды, `<Geom_CartesianPoint.hxx>` дважды (в части 1 было)

См. CR-252. Часть уже зафиксирована. Здесь дополнительно:
- `<Prs3d_LineAspect.hxx>` — не вижу, но используется (`Aspect_TOL_DASH`, `SetLineAspect`).
- `<Aspect_TypeOfLine.hxx>` — есть.
- `<Graphic3d_ZLayerId.hxx>` — используется `Graphic3d_ZLayerId_Topmost`, не вижу include.

**Фикс:** добавить пропущенные.

**Оценка:** 5 минут.

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

## 🔴 P0 — ВОЛНА 8 (OcctView часть 3: selection, camera, mouse)

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

### CR-305. `OnMouseButton` / `OnMouseMove` / `OnMouseScroll` — магические числа

**Где:**
- `OcctView.cpp:746`: `if (xMax - xMin > 5 && yMax - yMin > 5)` — 5 px
- `OcctView.cpp:807`: `if (dx*dx + dy*dy > 25.0)` — 5 px²
- `OcctView.cpp:749, 761`: `myContext->Select(...)` — параметры без имён

**Фикс:** `constexpr double kMinSelectionRectSize = 5.0;`, `constexpr double kDragStartToleranceSq = 25.0;`.

**Оценка:** 10 минут.

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

### CR-307. `OnMouseButton` — `myContext->MoveTo` и `Select` без проверки `myContext.IsNull()`

**Где:** `OcctView.cpp:738-747`

```
if (myLeftPressed && !myLeftDragging) {
    myContext->ClearSelected(false);      // ← нет проверки
    myContext->MoveTo(px, py, myView, false);
    myContext->SelectDetected(AIS_SelectionScheme_Replace);
    ...
}
```

`myContext` может быть `IsNull()` (если `Init` не был вызван). Падение.

**Хотя:** `OcctView::OnMouseButton` вызывается через GLFW-callback только после `Init`. Формально ок. Но защита от регрессий — нужна.

**Фикс:** ранний `if (myContext.IsNull()) return;` в начале.

**Оценка:** 2 минуты.

---

### CR-308. `OnMouseMove` — `myView->Pan()` без проверки `myView.IsNull()`

**Где:** `OcctView.cpp:817-821`

```
if (myPanning) {
    int lpx, lpy;
    CursorToFb(myGlfwWindow, myLastX, myLastY, lpx, lpy);
    myView->Pan(px - lpx, -(py - lpy));   // ← нет проверки
    myView->Redraw();
}
```

Аналогично CR-307. **Плюс:** `myView->Pan` тоже без проверки на null.

**Фикс:** ранний guard.

**Оценка:** 2 минуты.

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

**Где:** `OcctView.cpp:786`

```
rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), up), angleDeg * M_PI / 180.0);
```

**Фикс:** `constexpr double kDegToRad = M_PI / 180.0;` (в `util/Geometry2D` уже должно быть).

**Оценка:** 5 минут.

---

### CR-312. `CursorToFb` — статическая функция, дублирует логику из `Application`

**Где:** `OcctView.cpp:698-706`

```
static void CursorToFb(GLFWwindow* w, double x, double y, int& outX, int& outY) {
    int winW = 1, winH = 1, fbW = 1, fbH = 1;
    glfwGetWindowSize(w, &winW, &winH);
    glfwGetFramebufferSize(w, &fbW, &fbH);
    double sx = (winW > 0) ? (double)fbW / winW : 1.0;
    double sy = (winH > 0) ? (double)fbH / winH : 1.0;
    outX = (int)(x * sx);
    outY = (int)(y * sy);
}
```

**То же самое**, что `Application::Run` делает inline (CR-061). Вынести в `platform/glfw/GlfwWindow` helper.

**Оценка:** 20 минут.

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

### CR-320. `OcctView.cpp` — `M_PI` без явного `<cmath>` гарантии

**Где:** `OcctView.cpp:14` — есть `#include <cmath>`, но `M_PI` — не стандарт C++ (POSIX extension). На MSVC требует `_USE_MATH_DEFINES`. На MinGW работает. **Для кроссплатформенности** (macOS, ARM) — использовать `std::numbers::pi` (C++20) или свой `constexpr double kPi = 3.14159265358979323846;`.

**Оценка:** 5 минут.

---
