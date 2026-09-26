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
