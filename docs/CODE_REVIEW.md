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
