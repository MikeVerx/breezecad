# BreezeCAD — Review: P0 / theme

**Записей:** 35

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

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

---

### CR-326. `ThemeTokens` — нет токенов для ViewCube (есть, но не полные)

**Где:** `ThemeTokens.h:32-42`

11 полей `cube*` — это **хорошо**, покрытие есть. Но:
- Нет `cubeCornerHover`, `cubeEdgeHover` — для 12 рёбер / 8 углов (Фаза 2).
- Нет `cubeHighlight` — для текущего hover.

Пока не критично, **отложить до Фазы 2**.

**WONTFIX** (отложено).

---

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

---

### CR-334. `ThemeManager.h` — «legacy Dark» без объяснения

**Где:** `ThemeManager.h:15`

Что такое **legacy Dark**? `SetupDarkTheme` удалена в `b55fb70` (B-13). Зачем оставлять в комментарии? Если это значение совместимо со старым `imgui.ini` — ок, но не задокументировано.

**Фикс:** либо удалить упоминание, либо задокументировать сценарий.

**Оценка:** 5 минут.

---

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

---

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

---

### CR-341. `ApplyTheme` — magic `theme == 3`

**Где:** `ThemeManager.cpp:15`

```
if (theme == 3) theme = 1;
```

Связано с CR-332. Плюс — комментарий в шапке объясняет «3 = legacy Dark», но `theme` — это **int**, никакой защиты.

**Фикс:** `enum class Theme` (CR-332).

---

---

### CR-342. `ApplyTheme` — нет default-ветки

**Где:** `ThemeManager.cpp:15-105`

```
if (theme == 0) { ... } else { ... }
```

**Что если `theme == 5`?** Попадёт в `else` → тёмная. Логика работает, но неявно.

**Фикс:** `switch (theme)` с явным `default: case Dark`.

---

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

---

### CR-352. `ThemeManager.cpp` — `ApplyTheme` ~90 строк

**Где:** `ThemeManager.cpp:10-107`

Огромная функция. 2 большие ветки. Каждая — 40+ строк заполнения токенов.

**Фикс:** разбить на `FillLightTokens(myTheme)` + `FillDarkTokens(myTheme)` + `ApplyStylePreset(theme)`.

**Оценка:** 30 минут.

---

---

### CR-353. `SetupLightTheme` / `SetupDeepSTheme` — `ImVec4(BG_WINDOW.x, BG_WINDOW.y, BG_WINDOW.z, 0.0f)` — неочевидно

**Где:** `ThemePresets.cpp:62, 145`

```
c[ImGuiCol_ChildBg] = ImVec4(BG_WINDOW.x, BG_WINDOW.y, BG_WINDOW.z, 0.0f);
```

Alpha = 0 — прозрачность. Но зачем копировать RGB, если alpha = 0? **Фикс:** `ImVec4(0, 0, 0, 0)` или комментарий.

**Оценка:** 2 минуты.

---

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

---

### CR-355. `ThemeManager.h` — параметр `int theme` (не enum)

**Где:** `ThemeManager.h:15`

Подтверждает CR-332.

---

---

## 🔴 P0 — ui/widgets (Волна 9, часть 3)

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

---

### CR-600. 🟠 `ContextMenu3D` — `ImGui::TextDisabled("%s", name.c_str())` — имя серым

**Где:** `ContextMenu.cpp:61`

Имя выбранного объекта — **Disabled**. Пользователь видит тусклый текст. Обычно имя — **акцент**.

**Фикс:** `Text("%s", name)` или `TextColored(myTheme.textMain, ...)`.

**Оценка:** 5 минут.

---

---

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

---

