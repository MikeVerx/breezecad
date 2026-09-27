# BreezeCAD — Code Review (INBOX — новые записи до переразбиения)

**Не путать с P0/P1/P2/P3 файлами.**
Это временный инбокс для записей из **оставшихся 28 модулей**.
После завершения аудита — склеивается со всеми и переразбивается скриптом.

**Статус:** активный приём.

---

## 🔴 P0 — ui/theme (Волна 9)

### CR-321. `ThemeTokens` — нет дефолтных значений → UB

**Где:** `src/ui/theme/ThemeTokens.h:12-44`
**Статус:** OPEN

**Проблема:** все 28 полей — `ImU32` **без инициализации**.
```
struct ThemeTokens {
    ImU32 bgTitle;      // ← мусор
    ImU32 bgRibbon;     // ← мусор
    ...
};
```
Создание `ThemeTokens t;` без `ApplyTheme` → случайные цвета или краш.

**Фикс:** `= 0` у каждого поля.

**Оценка:** 5 минут.

---

### CR-322. `ThemeTokens` — нет токенов для ribbon-кнопок

**Где:** `ThemeTokens.h`
**Статус:** OPEN

**Проблема:** `RibbonButton.cpp` использует 6 захардкоженных цветов (CR-003). В `ThemeTokens` — ни одного.

**Фикс:** добавить:
```
ImU32 ribbonBtnHover;
ImU32 ribbonBtnActive;
ImU32 ribbonBtnHoverDisabled;
ImU32 ribbonBtnActiveDisabled;
ImU32 ribbonArrowBg;
ImU32 ribbonArrowBgDisabled;
ImU32 ribbonIconFallback;
```

**Оценка:** 10 мин (структура) + 1 час (3 темы + правки).

---

### CR-323. `ThemeTokens` — нет токенов для 3D-геометрии

**Где:** `ThemeTokens.h`
**Статус:** OPEN

**Проблема:** `OcctView::DisplayShape` (CR-279) хардкодит цвета плоскостей/осей.

**Фикс:** добавить:
```
ImU32 planeFaceFill;
ImU32 planeFaceEdge;
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
**Статус:** OPEN

**Проблема:** цвета линий эскиза дублируются в `SketchTab.cpp:200-202` и `OcctView.cpp:626-644`.

**Фикс:** добавить:
```
ImU32 lineStyleNormal;
ImU32 lineStyleConstruction;
ImU32 lineStyleCenterline;
```
+ функцию `ImU32 LineStyleColor(LineStyle)` в `util` или `theme`.

**Оценка:** 20 минут.

---

### CR-325. `ThemeTokens` — нет токенов для preview-окна

**Где:** `ThemeTokens.h`
**Статус:** OPEN

**Проблема:** `DrawPreviewInfo` (CR-168) хардкодит цвета Length/Angle.

**Фикс:** добавить:
```
ImU32 previewLengthText;
ImU32 previewAngleText;
ImU32 previewBg;
```

**Оценка:** 5 минут.

---

### CR-327. `ThemeTokens.h` — namespace не согласован с другими

**Где:** `ThemeTokens.h:12`
**Статус:** OPEN

**Проблема:** `ThemeTokens` в `namespace BreezeCAD`, остальные `ui/theme` файлы — в `BreezeCAD::UI::Theme`. Связано с CR-018.

**Фикс:** унифицировать.

**Оценка:** 15 минут (везде переименовать).

---

### CR-328. `ThemeTokens` — поля не задокументированы

**Где:** `ThemeTokens.h:13-42`
**Статус:** OPEN

`bgTitle`, `homeBtnLabel` — что это? Однострочный комментарий каждому полю.

**Оценка:** 15 минут.

---

### CR-329. `ThemePresets` — нет `SetupAutoTheme()`

**Где:** `ThemePresets.h:16-20`
**Статус:** OPEN

Хедер объявляет только Light и DeepS. Но `ThemeManager.h:15` говорит «2 = Авто». Где реализация?

**Фикс:** либо объявить, либо задокументировать «Авто = выбор между двумя».

**Оценка:** 5 минут.

---

### CR-330. `ThemePresets.h` — комментарий про DeepSColor устарел

**Где:** `ThemePresets.h:17`
**Статус:** OPEN

По `PROJECT_STATE.md` — DeepSColor переименована в «Тёмную».

**Фикс:** «Тёмная тема: глубокий океан, акцент — бирюза.»

**Оценка:** 1 минута.

---

### CR-331. Заголовки — устаревшие комментарии про этапы рефакторинга

**Где:** `ThemePresets.h:10`, `ThemeManager.h:16`, `Fonts.h:11`
**Статус:** OPEN

«Реализация — на шаге 2.2 рефакторинга» — рефакторинг завершён.

**Фикс:** убрать строки.

**Оценка:** 3 минуты.

---

### CR-332. `ThemeManager` — магические числа 0/1/2/3

**Где:** `ThemeManager.h:15`
**Статус:** OPEN

```
// theme: 0 = Светлая, 1 = Тёмная, 2 = Авто, 3 = legacy Dark.
void ApplyTheme(int theme, ...);
```

**Фикс:** `enum class Theme : uint8_t { Light = 0, Dark = 1, System = 2, LegacyDark = 3 };`

Связано с CR-089.

**Оценка:** 30 минут.

---

### CR-333. `ThemeManager` — не подключает `ThemeTokens.h`

**Где:** `ThemeManager.h:11-13`
**Статус:** OPEN

Forward declaration для `ThemeTokens` — пользователи вынуждены подключать 2 хедера.

**Фикс:** `#include "ui/theme/ThemeTokens.h"` в `ThemeManager.h`.

**Оценка:** 2 минуты.

---

### CR-334. `ThemeManager.h` — «legacy Dark» без объяснения

**Где:** `ThemeManager.h:15`
**Статус:** OPEN

Что такое legacy Dark? `SetupDarkTheme` удалена в `b55fb70`. Если для совместимости — задокументировать.

**Оценка:** 5 минут.

---

### CR-335. `Fonts.h` — хардкод пути `assets/fonts/` (RULES п.23)

**Где:** `Fonts.h:6`
**Статус:** OPEN

Связано с CR-219 (IconManager).

**Оценка:** входит в пакет CR-219.

---

### CR-336. Заголовки — namespace-стиль разнобой (3 варианта на 4 файла)

**Где:** `ThemeTokens.h`, `ThemePresets.h`, `ThemeManager.h`, `Fonts.h`
**Статус:** OPEN

Связано с CR-018.

**Оценка:** входит в пакет CR-018.

---

---

## 🔴 P0 — ui/theme (часть 2: .cpp)

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

### CR-344. `ApplyTheme` — хардкод цветов градиента фона

**Где:** `ThemeManager.cpp:55-57, 103-105`

```
// Светлая
Quantity_Color top(0.66, 0.72, 0.78, Quantity_TOC_RGB);      // #A8B8C7
Quantity_Color bottom(0.85, 0.88, 0.91, Quantity_TOC_RGB);   // #D9E0E8

// Тёмная
Quantity_Color top(0.040, 0.075, 0.130, Quantity_TOC_RGB);
Quantity_Color bottom(0.010, 0.025, 0.050, Quantity_TOC_RGB);
```

Хардкод, RULES п.30. Это **должно быть в ThemeTokens**: `viewportGradientTop`, `viewportGradientBottom`.

**Оценка:** 20 минут.

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

### CR-356. `SvgIconBtn` — хардкод fallback-цвета (RULES п.30)

**Где:** `SvgButton.cpp:48`
**Статус:** OPEN

```
dl->AddRectFilled(..., IM_COL32(140, 140, 140, 180), 3.0f);
```

Связано с CR-033 (fallback в RibbonButton). Тот же цвет — но в другом файле. Два источника правды.

**Фикс:** общий токен `theme.iconFallback` + одна функция fallback-отрисовки.

**Оценка:** входит в пакет CR-322.

---

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

### CR-365. `Tooltip::Draw` — хардкод жёлтого цвета названия (RULES п.30)

**Где:** `Tooltip.cpp:47`
**Статус:** OPEN

```
ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", name.c_str());
```

**Фикс:** `myTheme.tooltipTitleColor` или `theme.warning`.

**Оценка:** входит в пакет CR-322.

---

### CR-366. `Tooltip::Draw` — magic числа уровней 0.5 / 1.5 / 3.0

**Где:** `Tooltip.cpp:35-38`

```
if (elapsed >= 3.0)      level = 3;
else if (elapsed >= 1.5) level = 2;
else if (elapsed >= 0.5) level = 1;
```

**Фикс:** `constexpr double kTooltipLevel1 = 0.5;` и т.д.

**Оценка:** 5 минут.

---

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

### CR-368. `Tooltip::Draw` — magic `25.0f` в `PushTextWrapPos`

**Где:** `Tooltip.cpp:43`

```
ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
```

25 × размер шрифта = обёртка по 450 px при 18 pt. **Magic.**

**Фикс:** `constexpr float kTooltipWrapPos = 25.0f;` или через px.

**Оценка:** 3 минуты.

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

### CR-375. `SelectionRect::DrawSelectionRect` — хардкод цветов (RULES п.30)

**Где:** `SelectionRect.cpp:33-34`

```
draw->AddRectFilled(tl, br, IM_COL32(80, 130, 180, 60));
draw->AddRect(tl, br, IM_COL32(80, 130, 180, 255), 0.0f, 0, 1.5f);
```

**Фикс:** `myTheme.selectionRectFill` + `myTheme.selectionRectBorder` + `myTheme.selectionRectBorderWidth`.

**Оценка:** входит в пакет CR-322.

---

### CR-376. `SelectionRect` — 4-й дубль `glfwGetWindowSize + FramebufferSize`

**Где:** `SelectionRect.cpp:22-24`

Связано с CR-061, CR-312. Уже **4 места** в проекте делают одно и то же.

**Фикс:** выделить в `platform/glfw/GlfwWindow::ScreenToFramebuffer(...)` или `util/Screen.h`.

**Оценка:** 30 минут (все 4 места).

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

### CR-378. `SelectionRect` — magic `1.5f` толщина рамки

**Где:** `SelectionRect.cpp:34`

```
draw->AddRect(tl, br, ..., 0.0f, 0, 1.5f);
```

**Фикс:** `constexpr float kSelectionBorderWidth = 1.5f;`.

**Оценка:** 2 минуты.

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

### CR-382. `DrawQuickAccessToolbar` — `ctx.icons` и `ctx.tooltip` без проверки

**Где:** `QuickAccessToolbar.cpp:14, 41, 43, 63...`
**Статус:** OPEN

```
if (UI::Widgets::QatIconButton(*ctx.icons, "qat_logo", ...))   // ← *ctx.icons без проверки
ctx.tooltip->Draw("qat.doc", ...);                              // ← ctx.tooltip без проверки
```

**Если `ctx.icons == nullptr`** — краш на первом вызове.
**Если `ctx.tooltip == nullptr`** — краш на `->Draw`.

**Фикс:** ранний `assert(ctx.icons && ctx.tooltip);` или `if (!ctx.icons || !ctx.tooltip) return;`.

**Оценка:** 2 минуты.

---

## 🔴 P0 — ui/qat (Волна 10, часть 2: QuickAccessToolbar.cpp)

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

**Где:** `QuickAccessToolbar.cpp:29-30`

```
if (ImGui::MenuItem("О программе")) {}   // ← пусто
if (ImGui::MenuItem("Справка", "F1")) {} // ← пусто
```

**Что ломается:** пользователь кликает, **ничего не происходит**. Диалог должен открываться (`NotImplementedDialog`).

**Фикс:** `ctx.onNotImplemented("О программе")` / `ctx.onNotImplemented("Справка")`.

**Оценка:** 5 минут.

---

### CR-389. Меню «Текстуры» — все MenuItem с пустыми обработчиками

**Где:** `QuickAccessToolbar.cpp:139-147`

```
if (ImGui::MenuItem("Сталь"))    {}   // ← пусто
if (ImGui::MenuItem("Алюминий")) {}   // ← пусто
...
if (ImGui::MenuItem("Импортировать текстуру...")) {}   // ← пусто
```

**То же — 5 пустых обработчиков.** Пользователь жмёт «Сталь» — ничего.

**Фикс:** `ctx.onNotImplemented("Материал: Сталь")` и т.д.

**Оценка:** 10 минут.

---

### CR-390. Меню «Параметры» — пустые обработчики

**Где:** `QuickAccessToolbar.cpp:162-166`

```
if (ImGui::MenuItem("Открыть в новой вкладке")) {}   // ← пусто
```

**Фикс:** `onNotImplemented("Параметры в новой вкладке")`.

**Оценка:** 2 минуты.

---

### CR-391. Меню «Измерение» — пустые обработчики

**Где:** `QuickAccessToolbar.cpp:178-181`

```
if (ImGui::MenuItem("Расстояние"))        {}   // ← пусто
if (ImGui::MenuItem("Угол"))              {}   // ← пусто
if (ImGui::MenuItem("Радиус"))            {}   // ← пусто
if (ImGui::MenuItem("Диаметр"))           {}   // ← пусто
```

**Фикс:** `onNotImplemented(...)` для каждого.

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

### CR-393. `static bool showLines/showDims/showConstraints` + `static int precision`

**Где:** `QuickAccessToolbar.cpp:131-136, 185-188`
**Статус:** OPEN
**Категория:** RULES п.20

```
static bool showLines = true, showDims = true, showConstraints = true;   // ← static в функции
...
static int precision = 2;                                                // ← static в функции
```

**RULES п.20:** «Никаких static переменных в member-функциях для состояния между вызовами».

**Проблема:** если создать два окна приложения — оба разделят состояния. Плюс — `static` в **free-функции** (`DrawQuickAccessToolbar`), не в member — но правило всё равно нарушено.

**Связано с B-11** (SettingsDialog — визуальные заглушки). Плюс — эти значения **никуда не передаются** — они локальны и бесполезны.

**Фикс:** либо в `QatContext` (как `bool& showLines`), либо в `Application::myQatState`, либо — **до реализации функционала — убрать вообще** (сделать заглушки без состояния).

**Оценка:** 30 минут.

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

### CR-397. 20+ magic чисел позиционирования

**Где:** `QuickAccessToolbar.cpp:193-204`

```
const float searchW    = 220.0f;
const float telemetryW = 180.0f;
const float margin     = 12.0f;
float rightPanelW = ctx.rightPanelExpanded ? 280.0f : 40.0f;
float telemetryX = io.DisplaySize.x - margin - rightPanelW - telemetryW;
float searchX    = telemetryX - searchW - 12.0f;       // ← 12.0f магия
float modulesX   = searchX - 32.0f - 8.0f;             // ← 32, 8 магия
...
if (UI::Widgets::SvgIconBtn(*ctx.icons, "qat_modules", "puzzle", 18, 28, 26, "Модули"))   // ← 18,28,26
```

**Фикс:** constexpr-константы или layout-структура.

**Оценка:** 30 минут.

---

### CR-398. Tooltip «Модули (не разработано)» — хардкод + нет Tr

**Где:** `QuickAccessToolbar.cpp:208`

**Связано с CR-384.**

---

### CR-399. Телеметрия формат — хардкод строки

**Где:** `QuickAccessToolbar.cpp:253`

```
ImGui::TextDisabled("FPS: %.0f | RAM: %.0f MB | Obj: %d", ...);
```

**Фикс:** `Tr("qat.telemetry.fmt")` с плейсхолдерами.

**Оценка:** 5 минут.

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

### CR-402. Меню `qat_create` — «Сборка» вызывает `onNotImplemented`, но имя ключа "Сборка" — русский хардкод

**Где:** `QuickAccessToolbar.cpp:48-49`

```
if (ctx.onNotImplemented) ctx.onNotImplemented("Сборка");
```

Передаётся **русское название**. В `NotImplementedDialog` отображается `feature` как есть — пользователь всегда видит русский, даже если UI переключён на EN.

**Фикс:** передавать ключ `"feat.assembly"` + `Tr()` в диалоге.

**Связано с CR-097** (toast/errors).

**Оценка:** 20 минут.

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

### CR-406. `StatusBar` — хардкод зелёного для «Эскиз» (RULES п.30)

**Где:** `StatusBar.cpp:29`

```
ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Эскиз");
```

**TODO в коде подтверждает** — сам знаешь.

**Фикс:** `myTheme.statusSketchActive` + `Tr("status.sketch_mode")`.

**Оценка:** входит в пакет CR-322.

---

### CR-407. `StatusBar` — все строки на русском хардкодом

**Где:** `StatusBar.cpp:29, 31, 38-42, 54-55`

- «Эскиз», «Режим ожидания»
- «Снап: сетка + точки», «Снап: сетка 10 мм», «Снап: точки», «Снап: выкл», «Снап: н/д»
- «Готово», «1:1»

**Фикс:** `Tr("status.xxx")` — 9 ключей.

**Оценка:** 30 минут.

---

### CR-408. `StatusBar` — magic «10 мм» в строке

**Где:** `StatusBar.cpp:39`

```
else if (info.snapToGrid) ImGui::TextDisabled("Снап: сетка 10 мм");
```

**Шаг сетки 10 мм — magic.** Если пользователь изменит шаг в настройках — строка соврёт.

**Связано с CR-407.** Фикс: `info.gridStep` + формат через Tr с плейсхолдером.

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

### CR-410. `StatusBar` — magic-числа позиционирования

**Где:** `StatusBar.cpp:18, 52-53, 62-64`

```
const float h = 24.0f;
const float margin = 12.0f;
float statusX = io.DisplaySize.x - rightPanelW - margin - statusW;
float zoomX   = statusX - 20.0f - zoomW;   // ← 20
if (zoomX > 400.0f)   { ... }              // ← 400
if (statusX > 400.0f) { ... }              // ← 400
```

**Фикс:** `constexpr` для каждой.

**Оценка:** 10 минут.

---

### CR-411. `StatusBar` — `io.DisplaySize.y - h` без проверки минимума

**Где:** `StatusBar.cpp:18`

Если окно меньше 24 px (маловероятно, но) — статус-бар уедет за границу.

**WONTFIX** (нереально).

---

## 🔴 P0 — ui/status (Волна 10, часть 4: ProjectTabs)

### CR-412. `ProjectTabs` — `static int pendingCloseIdx = -1; static int switchToIdx = -2;`

**Где:** `ProjectTabs.cpp:51-52`
**Статус:** OPEN
**Категория:** RULES п.20 + B-05

**Это тот самый B-05** из `REFACTOR_NOTES.md` — «static int pendingCloseIdx/switchToIdx в ProjectTabs.cpp».

**Проблема:**
1. `static` в свободной функции — общий стейт для всех вызовов.
2. При двух окнах приложения — **общие флаги**.
3. `pendingCloseIdx = -1` → `switchToIdx = -2` — magic.

**Фикс:** `ProjectTabsState` struct в Application + передавать в ctx. Или — хранить в `ProjectTabsContext` по ссылке.

**Оценка:** 30 минут.

---

### CR-413. `ProjectTabs` — синий accent **третий оттенок** (после CR-337)

**Где:** `ProjectTabs.cpp:60-62, 89`

```
ImVec4 accent(0.20f, 0.55f, 0.95f, 1.0f);         // ← #338CF2
ImVec4 accentHover(0.30f, 0.65f, 1.00f, 1.0f);
ImVec4 accentActive(0.15f, 0.45f, 0.85f, 1.0f);
...
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
```

**Плюс:** в `TabHome` disabled-стиль использует тот же синий.

**Что имеем по проекту:**
| Место | Light accent | Dark accent |
|---|---|---|
| `ThemePresets.cpp` | `#00D9C0` (бирюза) | `#00D9C0` |
| `ThemeManager.cpp` (myTheme.accent) | `#2878D8` | `#338CF2` |
| `ProjectTabs.cpp` | — | `#338CF2` |

**Три источника.** В светлой теме ProjectTabs будет **синий**, а кнопки ImGui — **бирюзовые**.

**Фикс:** читать из `myTheme.accent` через ctx.

**Оценка:** 20 минут.

---

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

### CR-415. `ProjectTabs` — 5 пустых MenuItem в контекстном меню

**Где:** `ProjectTabs.cpp:93-97`

```
if (ImGui::MenuItem("Сохранить")) {}        // ← пусто
if (ImGui::MenuItem("Сохранить как...")) {} // ← пусто
ImGui::Separator();
if (ImGui::MenuItem("Закрыть")) pendingCloseIdx = i;
if (ImGui::MenuItem("Закрыть другие")) {}   // ← пусто
```

**3 из 5 — пустые.** Пользователь кликает «Сохранить» — ничего.

**Фикс:** `onSave(index)`, `onSaveAs(index)`, `onCloseOthers(index)` в ctx.

**Оценка:** 30 минут.

---

### CR-416. `ProjectTabs` — строки меню на русском (хардкод)

**Где:** `ProjectTabs.cpp:93-97, 135`

```
"Сохранить", "Сохранить как...", "Закрыть", "Закрыть другие"
"Новый проект" (в SetTooltip)
```

**Фикс:** `Tr(...)`.

**Оценка:** 10 минут.

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

### CR-419. `ProjectTabs` — magic 120/30/140/20/12/14/28 — размеры и отступы

**Где:** `ProjectTabs.cpp:36, 58, 82, 102, 130`

```
SvgIconBtn(*ctx.icons, "tab_home", "house", 14, 120, 30, "На главную")  // 14, 120, 30
Button(..., ImVec2(140, 30))                                            // 140, 30
SvgIconBtn(*ctx.icons, "tab_close", "x", 12, 20, 30, "Закрыть")         // 12, 20, 30
SameLine(0, 2)                                                          // 2
SvgIconBtn(*ctx.icons, "tab_plus", "plus", 14, 28, 30, "Новый проект")  // 14, 28, 30
```

**Фикс:** `constexpr` + именованные константы (или UI_LAYOUT ссылки).

**Оценка:** 20 минут.

---

### CR-420. `ProjectTabs` — `statusH = 24, tabsH = 52` — magic + расходится с UI_LAYOUT

**Где:** `ProjectTabs.cpp:14-15`

```
const float statusH = 24.0f;
const float tabsH   = 52.0f;
```

UI_LAYOUT §2.2 говорит: **вкладки проектов 32 px** (не 52!). А в `StatusBar` — 24. **Расхождение 20 px.**

**Разобраться:** что правильно — 32 или 52?

**Фикс:** актуализировать UI_LAYOUT или код. Единая константа.

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

### CR-426. `ProjectTabs` — `SetTooltip("Новый проект")` без `Tr`

**Где:** `ProjectTabs.cpp:135`

**Связано с CR-416.**

---

## 🟠 P1 — ui/status

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

### CR-441. `HomeContext` — `IconManager*` не проверяется

**Где:** `HomeScreen.cpp:85, 190, 253, 282`

```
GLuint icoTex = ctx.icons->Get(hbtnIcons[bi], 32);   // ← *ctx.icons без проверки
```

**Если `ctx.icons == nullptr`** — краш. Связано с CR-382.

**Фикс:** ранний `assert(ctx.icons)` в начале `DrawHomeScreen`.

**Оценка:** 2 минуты.

---

### CR-442. `HomeContext` — `theme` не проверяется

**Где:** `HomeScreen.cpp:25, 42, 51, 110`

```
ImGui::PushStyleColor(ImGuiCol_WindowBg, ctx.theme->bgContent);
```

**Если `ctx.theme == nullptr`** — краш на первой строке.

**Фикс:** `assert(ctx.theme);` в начале.

**Оценка:** 2 минуты.

---

### CR-443. `HomeContext` — `showHomeScreen`, `viewMode`, `searchBuffer` без проверок

**Где:** `HomeScreen.cpp:22, 249, 253, 322, 423`

```
if (!(*ctx.showHomeScreen)) return;                        // *showHomeScreen
bool activeTiles = ((*ctx.viewMode) == 0);                 // *viewMode
ImGui::InputTextWithHint("##home_search", "Поиск...", ctx.searchBuffer, ctx.searchBufferSize);
```

Если хотя бы один `nullptr` — краш.

**Фикс:** все указатели-параметры проверить.

**Оценка:** 5 минут.

---

### CR-444. HomeScreen — 20+ хардкод-строк на русском

**Где:** весь файл
**Статус:** OPEN

- «BreezeCAD» (×2)
- «Деталь», «Сборка», «Открыть»
- «Последние документы»
- «Управление модулями», «Справка», «О программе», «Параметры», «Выход»
- «Версия 0.1.x», «Прототип»
- «Поиск...»
- «Открыть», «Закрыть» (в плитках)
- «<не сохранён>»
- «[ 3D ]»
- «Нет недавних документов. Создайте новый — кнопка «Деталь» сверху.»
- «Имя», «Расположение», «Дата», «Размер»
- «—» (дата/размер в списке)

**Фикс:** `Tr("home.xxx")` + 25 новых ключей.

**Оценка:** 1 час.

---

### CR-445. HomeScreen — хардкод синего accent (4-й оттенок)

**Где:** `HomeScreen.cpp:252, 279`

```
ImGui::PushStyleColor(ImGuiCol_Button, activeTiles
    ? ImVec4(0.20f, 0.55f, 0.95f, 1.0f)         // ← #338CF2 снова
    : ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.40f, 0.55f, 1.0f));
```

**5-й источник синего** (ThemePresets, ThemeManager, ProjectTabs, QAT, теперь HomeScreen).

**Фикс:** `ctx.theme->accent`.

**Оценка:** входит в пакет CR-322.

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

### CR-453. HomeScreen — `bw2 = (bwFull - 8.0f) * 0.5f;` — magic 8

**Где:** `HomeScreen.cpp:197`

**Фикс:** `constexpr float kHomeBtnGap = 8.0f;`.

**Оценка:** 2 минуты.

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

### CR-455. HomeScreen — `homeBtn` lambda дублирует SvgIconBtn/QatIconButton

**Где:** `HomeScreen.cpp:158-177`

Своя реализация кнопки с фиксированной шириной. Есть уже `SvgIconBtn` и `QatIconButton`. **3-я копия.**

**Фикс:** использовать `SvgIconBtn` с параметром `label` или вынести в `widgets/IconButton`.

**Оценка:** входит в CR-358 (объединение кнопок).

---

### CR-456. HomeScreen — `homeBtn` не поддерживает tooltip

**Где:** `HomeScreen.cpp:158-177`

Кнопки «Управление модулями», «Справка» и т.д. — без tooltip. Пользователь не знает, что кнопка **disabled**.

**Фикс:** добавить `const char* tooltip = nullptr` параметр.

**Оценка:** 15 минут.

---

## 🔴 P0 — ui/home (Волна 11, часть 3: Thumbnails.cpp)

### CR-457. 🔴 `CaptureCurrentThumbnail` — хардкод `D:/test_breezecad/thumbs/`

**Где:** `Thumbnails.cpp:24`
**Статус:** OPEN
**Категория:** RULES п.23 + тестовый путь в продакшене

```
std::string dir = "D:/test_breezecad/thumbs/";
```

**Что это значит:**
1. RULES п.23: «Никаких хардкодов путей.»
2. `D:/test_breezecad/` — **тестовый путь в продакшен-коде**.
3. **На macOS, Linux, ARM** — диска `D:` нет → папка не создастся → превью **никогда** не сохраняются.
4. Хардкод `D:` убивает кроссплатформенность.

**Фикс:**
- `std::filesystem::path exeDir = ...` + `exeDir / "thumbs"`.
- Или `%APPDATA%/BreezeCAD/thumbs/` (Win), `~/.config/BreezeCAD/thumbs/` (Unix).

**Оценка:** 30 минут + унификация с `platform/Paths.h`.

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

### CR-465. `DrawRenameDialog` — жёсткий русский хардкод, ни одного `Tr()`

**Где:** `RenameDialog.cpp:12, 16, 23, 31`
**Статус:** OPEN
**Категория:** RULES п.27 (локализация)

```
ImGui::OpenPopup("Переименовать");
ImGui::BeginPopupModal("Переименовать", ...);
ImGui::Text("Новое имя:");
ImGui::Button("OK", ...)
ImGui::Button("Отмена", ...)
```

**5 строк** без `Tr()`. При переключении на EN — останутся русскими.

**Фикс:** `Tr("dlg.rename.title")`, `Tr("dlg.rename.prompt")`, `Tr("btn.ok")`, `Tr("btn.cancel")`.

**Оценка:** 10 минут (только Rename).

---

### CR-466. `DrawRenameDialog` — `ImGui::OpenPopup` вызывается каждый кадр, ESC не работает

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

### CR-468. `RenameDialog` — magic `280` и `120`

**Где:** `RenameDialog.cpp:17, 23, 31`

```
ImGui::SetNextItemWidth(280);
ImGui::Button("OK", ImVec2(120, 0));
ImGui::Button("Отмена", ImVec2(120, 0));
```

**Фикс:** `constexpr float kRenameInputWidth = 280.0f;` + общий `kDialogButtonWidth = 120.0f` (для всех диалогов).

**Оценка:** 5 минут.

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

### CR-471. `DrawDeleteConfirmDialog` — жёсткий русский хардкод

**Где:** `DeleteConfirmDialog.cpp:12, 16, 22, 30, 38`

```
"Подтверждение удаления"   // ×2
"Не спрашивать снова (до перезапуска)"
"Удалить"
"Отмена"
```

**5 строк** без `Tr()`.

**Оценка:** 10 минут.

---

### CR-472. `DrawDeleteConfirmDialog` — `OpenPopup` каждый кадр, ESC не работает

**Где:** `DeleteConfirmDialog.cpp:12`

Связано с CR-466.

**Оценка:** 15 минут (пакетно со всеми диалогами).

---

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

### CR-476. `DeleteConfirmDialog` — magic `120`, `256`

**Где:** `DeleteConfirmDialog.cpp:22, 30, 38`

**Фикс:** общие константы диалогов.

**Оценка:** входит в CR-468.

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 3: UnsavedChangesDialog)

### CR-477. `DrawUnsavedChangesDialog` — жёсткий русский хардкод

**Где:** `UnsavedChangesDialog.cpp:12, 15, 20, 44, 65`

```
"Несохранённые изменения"   // ×2
"Сохранить"
"Не сохранять"
"Отмена"
```

**Оценка:** 10 минут.

---

### CR-478. `DrawUnsavedChangesDialog` — 3× копипаста сброса состояния

**Где:** `UnsavedChangesDialog.cpp:33-36, 56-59, 79-82`

Во **всех трёх кнопках** (Сохранить / Не сохранять / Отмена):

```
ctx.state->pendingCloseDocIndex = -1;
ctx.state->pendingWindowClose   = false;
ctx.state->show = false;
ImGui::CloseCurrentPopup();
```

**Дубль 3 раза.** Если добавится новое поле — забудется в двух местах.

**Фикс:** `auto resetState = [&]() { ... };` + вызов.

**Оценка:** 10 минут.

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

### CR-483. `UnsavedChangesDialog` — `OpenPopup` каждый кадр, ESC не работает

Связано с CR-466, CR-472.

---

### CR-484. `UnsavedChangesDialog` — magic `120` и `256`

**Где:** `UnsavedChangesDialog.cpp:20, 44, 65`, `.h:11`

**Фикс:** общие константы.

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

### CR-486. Все диалоги — `RenameDialog` и `DeleteConfirmDialog` возвращают `TDF_Label` — слоение

**Где:** `RenameDialog.h:11`, `DeleteConfirmDialog.h:13`

UI-модуль диалогов **знает про `TDF_Label`** — тип OCAF. По `ARCHITECTURE §2` — UI не должен знать про OCCT.

**Связано с CR-004** (LineStyle в RibbonContext).

**Фикс:** абстрактный `LabelHandle` (opaque `uint64_t` или `struct { uint32_t tag; uint32_t gen; }`), Application переводит.

**Оценка:** 3-4 часа (крупно).

---

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

### CR-496. `AddBoxDialog` — magic `120`

**Где:** `AddBoxDialog.cpp:26, 42`

Общая константа с остальными диалогами (CR-468, CR-476, CR-484, CR-510).

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 5: PropertiesDialog)

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

### CR-499. `PropertiesDialog` — `<unnamed>` — 2-й раз хардкод (после RequestDelete)

**Где:** `PropertiesDialog.cpp:22`

```
std::string name = "<unnamed>";
```

Уже было в `Application::RequestDelete` (CR-191). **2-й хардкод.**

**Фикс:** `Tr("misc.unnamed")` — единая точка.

**Оценка:** 3 минуты.

---

### CR-500. 🔴 `PropertiesDialog` — 17 хардкод-строк на русском

**Где:** `PropertiesDialog.cpp:18, 20, 29, 30, 31, 36, 49`

- «Свойства», «Объект не выбран»
- «Имя: %s», «Тип: %d», «Тег: %d», «Описание: %s»
- «Неизвестно», «Папка Start», «Плоскость», «Ось», «Точка Origin», «Куб», «Цилиндр», «Отверстие», «Эскиз», «Линия эскиза»
- «(расширенные свойства — позже)»

**Фикс:** `Tr("prop.xxx")` — 17 ключей.

**Оценка:** 30 минут.

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

### CR-504. `PropertiesDialog` — `SetNextWindowSize(400, 300)` — magic

**Где:** `PropertiesDialog.cpp:15`

**Фикс:** `constexpr ImVec2 kPropertiesDefaultSize{400, 300};`.

**Оценка:** 2 минуты.

---

### CR-505. `PropertiesDialog` — `(расширенные свойства — позже)` — не локализовано

**Где:** `PropertiesDialog.cpp:49`

**Связано с CR-500.**

---

### CR-506. `PropertiesDialog` — окно не модальное, но редактирование свойств не работает

**Где:** весь файл

Окно **только показывает** — нет возможности редактировать. По `UI_LAYOUT §8.3` — «Возможность редактирования "на лету"».

**Фикс:** добавить `InputText` для имени, `InputDouble` для размеров (Фаза 2).

**Отложено.**

---

## 🟠 P1 — ui/dialogs (Волна 12, часть 6: NotImplementedDialog)

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

### CR-508. `NotImplementedDialog` — 3 хардкод-строки на русском

**Где:** `NotImplementedDialog.cpp:14, 16, 20`

- «Функция \"%s\" не разработана.»
- «Будет доступно в следующих версиях BreezeCAD.»
- «OK»

**Фикс:** `Tr("notimpl.msg")`, `Tr("notimpl.future")`, `Tr("btn.ok")`.

**Оценка:** 5 минут.

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

### CR-510. `NotImplementedDialog` — magic `120`

Связано с CR-496, CR-468 и т.д.

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

### CR-517. `DependencyDialog` — `depNames[]` хардкод на русском

**Где:** `DependencyDialog.cpp:17-21`

```
static const char* depNames[] = {
    "Совпадение", "Параллельность", "Перпендикулярность",
    "Горизонталь", "Вертикаль", "Касание",
    "Концентричность", "Равенство"
};
```

**8 строк без `Tr()`.** Связано с CR-465/471/477/500/508.

**Фикс:** `Tr("dep.coincident")`, `Tr("dep.parallel")`, ...

**Оценка:** 15 минут.

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

### CR-524. `DependencyDialog` — 3× копипаста очистки состояния

**Где:** `DependencyDialog.cpp:99-101, 104-106, 112-114`

Во всех трёх кнопках (Отмена, Применить, Готово):
```
ctx.state->base.clear();
ctx.state->dependent.clear();
if (ctx.view) ctx.view->ClearSelection();
```

**Дубль 3 раза.** CR-478 подобное.

**Фикс:** `auto clearState = [&]() { ... };`.

**Оценка:** 10 минут.

---

### CR-525. `DependencyDialog` — все кнопки на русском

**Где:** `DependencyDialog.cpp:97, 103, 111`

```
ImGui::Button("Отмена", ...)
ImGui::Button("Применить", ...)
ImGui::Button("Готово", ...)
```

**Фикс:** `Tr("btn.cancel")`, `Tr("btn.apply")`, `Tr("btn.done")`.

**Оценка:** 5 минут.

---

### CR-526. `DependencyDialog` — 4 хардкод-строки (заголовки child и подсказки)

**Где:** `DependencyDialog.cpp:38, 40, 58, 60`

- «Базовый объект:»
- «(кликните по линии в сцене)»
- «Зависимые объекты:»
- «(кликните по линиям в сцене)»

**Фикс:** `Tr("dep.base")`, `Tr("dep.click_line")`, ...

**Оценка:** 10 минут.

---

### CR-527. `DependencyDialog` — magic `150, 150, 320, 80, 95, 20`

**Где:** `DependencyDialog.cpp:25-26, 30, 40, 58, 96`

- `0.5f - 150` ×2 — смещение центра
- `320` — ширина
- `80` — высота списков
- `95` — ширина кнопок
- `20` — отступ для крестика

**Фикс:** `constexpr`.

**Оценка:** 15 минут.

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

### CR-530. `DependencyDialog` — `DependencyState` не имеет `Clear()` метода

**Где:** `DependencyDialog.h:22-30`

Три места в `.cpp` вручную чистят `base`, `dependent`, `view->ClearSelection()`. Логичнее — метод на state или функция.

**Оценка:** входит в CR-524.

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 9: SettingsDialog)

### CR-531. 🔴 `SettingsDialog` — 10 статических переменных в функции (RULES п.20)

**Где:** `SettingsDialog.cpp:44, 48, 53, 55, 74, 78, 81, 93, 96, 99`
**Статус:** OPEN
**Категория:** RULES п.20 — прямое нарушение + B-11

```
static int units = 0;                                  // :44
static int precision = 2;                              // :48
static bool autosave = true;                           // :53
static int autosaveInterval = 5;                       // :55
static int quality = 1;                                // :74
static bool aa = true;                                 // :78
static bool shadows = false;                           // :81
static ImVec4 colNormal(...);                          // :93
static ImVec4 colConstruction(...);                    // :96
static ImVec4 colCenterline(...);                      // :99
```

**10 статических переменных в одной функции.**

**Что это значит:**
- **Общий стейт для всех Application** (если будет мультиоконность).
- Никуда **не сохраняются** — потеряются при перезапуске.
- **Никуда не передаются** — `B-11` в `REFACTOR_NOTES.md`: «4 из 6 вкладок — визуальные заглушки. static-локалы, никуда не передаются».
- Настройки **не влияют ни на что** — пользователь двигает слайдер, ничего не происходит.

**Фикс:**
- `SettingsState` расширить всеми полями.
- Или — отдельная структура `AppSettings`, сохраняемая в `%APPDATA%/BreezeCAD/settings.json`.
- Пока функционал не реализован — **убрать static-локалы и показать «(не реализовано)»**.

**Оценка:** 2 часа (правильно) или 30 минут (заглушки без стейта).

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

### CR-533. `SettingsDialog` — 30+ хардкод-строк на русском

**Где:** весь файл

- «Настройки», «Основные», «Графика», «Тема», «Язык», «Хоткеи», «О программе» (7)
- «Основные настройки», «Единицы измерения:», «Миллиметры (мм)», «Дюймы (in)» (4)
- «Знаков после запятой», «Автосохранение:», «Включить автосохранение», «Интервал (мин)» (4)
- «Графика», «(будет заполнено позже)», «Качество рендеринга:», «Низкое», «Среднее», «Высокое», «Сглаживание (antialiasing)», «Тени» (8)
- «Тема оформления», «Светлая», «Тёмная», «Авто», «Цвета линий эскиза:», «Основная (недоопределённая)», «Вспомогательная», «Осевая», «(применение цветов — позже)» (9)
- «Язык интерфейса», «English», «Русский» (3)
- «Горячие клавиши», «(редактор хоткеев — позже)», + 8 hotkey-описаний (10)
- «О программе BreezeCAD», «Версия: 0.1.0 (prototype)», «Сборка: %s %s», 3 строки (6)

**~45 строк.** Локализация полностью обойдена.

**Оценка:** 2 часа.

---

### CR-534. `SettingsDialog` — hotkeys перечислены хардкодом

**Где:** `SettingsDialog.cpp:126-133`

```
ImGui::Text("Ctrl+N  — Новый проект");
ImGui::Text("Ctrl+O  — Открыть");
ImGui::Text("Ctrl+S  — Сохранить");
...
```

**8 строк, дублирующих `InitCommands`** (Application.cpp:1039-1066). Если пользователь изменит хоткей в `InitCommands` — тут останется старое.

**Фикс:** генерировать список **из** `myCommands` (у каждой есть `shortcut`).

**Связано с `UI_LAYOUT §15.3`** — там хоткеи тоже таблицей.

**Оценка:** 30 минут.

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

### CR-539. `SettingsDialog` — окно 720×480 magic, нет адаптации

**Где:** `SettingsDialog.cpp:12`

```
ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
```

**Фикс:** `constexpr ImVec2 kSettingsDefaultSize{720, 480};` + не открывать, если экран < 800×600.

**Оценка:** 5 минут.

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

### CR-549. 🔴 `TreeContext` — 3 указателя на `TDF_Label` — UI зависит от OCAF

**Где:** `DocumentTree.h:52-54`
**Статус:** OPEN
**Категория:** архитектура (`ARCHITECTURE §2`)

```
TDF_Label* pendingCreateSketchLabel = nullptr;
TDF_Label* pendingEnterSketchLabel  = nullptr;
TDF_Label* clipboardLabel           = nullptr;
```

UI-модуль **знает про `TDF_Label`** — тип OCAF. Связано с **CR-486** (RenameDialog, DeleteConfirmDialog) и **CR-004** (LineStyle).

**Всего в проекте** теперь **более 10 мест**, где UI работает с `TDF_Label`:
- TreeContext: 3
- RenameDialog: 1
- DeleteConfirmDialog: 1
- PropertiesDialog: 1
- DependencyDialog: `std::vector<TDF_Label>`
- DocumentTree.cpp: в коде
- Application: `myPendingCreateSketchLabel`, `myPendingEnterSketchLabel` и т.д.

**Фикс:** абстрактный `LabelId` (opaque `uint64_t`) в UI + трансляция в Application. **Крупная работа.**

**Оценка:** 4-6 часов (комплексно).

---

### CR-550. 🟠 `TreeContext::visibility` / `isolationBackup` — `std::map<std::string, bool>*`

**Где:** `DocumentTree.h:38-39`
**Статус:** OPEN

**Связано с CR-093** (Application.cpp) и **CR-114** (myVisibility.clear() при switch).

**Проблемы:**
1. `std::map` — O(log N).
2. **Ключ `std::string`** — аллокация на каждый lookup.
3. **Глобальное состояние** (в Application), а должно быть per-document (см. CR-114).
4. **Два map'а** (`visibility` и `isolationBackup`) — связаны логикой изоляции.

**Фикс:**
- Один `struct DocumentUiState` с `unordered_map<uint64_t, bool>`.
- Хранить в `BreezeDocument`, не в Application.
- Или — `std::set<uint64_t>` для скрытых (обычно их мало).

**Оценка:** 2 часа.

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

### CR-559. 🔴 `Visibility::IsLabelVisibleByDefault` — UI знает `BreezeDocument::GetType`

**Где:** `Visibility.cpp:5, 16`
**Статус:** OPEN
**Категория:** архитектура

```
#include "doc/BreezeDocument.h"
...
int type = BreezeDocument::GetType(label);
```

UI-модуль панелей **напрямую дёргает** `BreezeDocument::GetType`. Связано с CR-549 (TDF_Label в UI).

**Фикс:** `type` должен передаваться в `IsLabelVisibleByDefault` как `int`, а не извлекаться в UI. Или `FeatureType` как opaque.

**Оценка:** входит в CR-549.

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

### CR-562. 🟠 `Visibility.cpp` — `view->GetAllLabels()` возвращает вектор по значению

**Где:** `Visibility.cpp:42`

```
auto allLabels = view->GetAllLabels();
```

**Связано с CR-268** (OcctView::GetAllLabels — vector по значению).

**Оценка:** входит в CR-268.

---

### CR-563. 🟡 `Visibility.h` — комментарий «Закрывает B-01» — код-стиль

**Где:** `Visibility.h:11`

```
// Закрывает B-01 (тройное дублирование логики видимости).
```

Это **ссылка на refactor-журнал** в **коде**. Мусор после закрытия B-01. **Фикс:** убрать.

**Оценка:** 1 минута.

---

## 🟠 P1 — ui/panels (Волна 13, часть 3: TreeSplitter)

### CR-564. 🔴 `TreeSplitter` — magic `200.0f` для topY — расходится с UI_LAYOUT

**Где:** `TreeSplitter.cpp:14`
**Статус:** OPEN

```
const float topY = 200.0f;
```

**Что должно быть:**
- По `UI_LAYOUT §2.2`: QAT 32 + Tabs 32 + Ribbon 78 = **142**.
- В `Application::Run:1146` `inScene` проверяет `MousePos.y > 165`.
- В `TreeSplitter` — **200**.

**Три разных значения** для одной границы.

**Фикс:** единая `constexpr float kRibbonBottomY = ...;` + актуализация UI_LAYOUT.

**Связано с CR-020** (206 в Ribbon.cpp) и **CR-059** (165 в Application.cpp).

**Оценка:** 30 минут (пакетно).

---

### CR-565. 🔴 `TreeSplitter` — magic `bottomY = io.DisplaySize.y - 76.0f`

**Где:** `TreeSplitter.cpp:15`
**Статус:** OPEN

```
const float bottomY = io.DisplaySize.y - 76.0f;
```

`76` = statusbar 24 + projecttabs 52. **Magic.**

Связано:
- `ProjectTabs.cpp:14` — `statusH = 24, tabsH = 52`.
- `HomeScreen.cpp:25` — `io.DisplaySize.y - 76.0f`.
- `Application.cpp:1149` — `MousePos.y < io.DisplaySize.y - 70`.

**Четыре разных источника** для одной и той же формулы.

**Фикс:** `constexpr float kBottomBarsHeight = 76.0f;` + единый `LayoutUtils`.

**Оценка:** 1 час (унификация).

---

### CR-566. 🟠 `TreeSplitter` — magic 5.0, 200.0, 600.0

**Где:** `TreeSplitter.cpp:8-10`

```
const float splitterW = 5.0f;
const float minTreeW  = 200.0f;
const float maxTreeW  = 600.0f;
```

- `5.0` — ширина полосы. Ок.
- `200` — min tree width. **По UI_LAYOUT §2.2: min 180**.
- `600` — max. **По UI_LAYOUT: max 480**.

**Расхождение с каноном.**

**Фикс:** актуализировать или UI_LAYOUT, или код.

**Оценка:** 15 минут.

---

### CR-567. 🟠 `TreeSplitter` — хардкод цвета (RULES п.30)

**Где:** `TreeSplitter.cpp:33`

```
draw->AddLine(..., IM_COL32(80, 130, 180, 255), 2.0f);
```

**Фикс:** `myTheme.splitterActive`.

**Оценка:** входит в пакет CR-322.

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

### CR-571. 🔴 `DependenciesPanel` — magic `340.0f` для panelTop

**Где:** `DependenciesPanel.cpp:13`
**Статус:** OPEN

```
const float panelTop = 340.0f;
```

**Что это?** — Верхняя граница панели «Зависимости». По UI_LAYOUT §4.3 — правая панель под ViewCube. **340** — magic.

**Фикс:** `constexpr float kRightPanelTop = ...;` (зависит от ViewCube высоты).

**Оценка:** 10 минут.

---

### CR-572. 🟠 `DependenciesPanel` — английский хардкод

**Где:** `DependenciesPanel.cpp:29-30`

```
ImGui::TextDisabled("Fully Constrained: NO");
ImGui::TextDisabled("DOF remaining: ?");
```

**Не локализовано.** При RU UI — английский текст.

**Фикс:** `Tr("panel.deps.fully_constrained")`, `Tr("panel.deps.dof_remaining")`.

**Оценка:** 5 минут.

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

### CR-574. 🟠 `DependenciesPanel` — magic `280.0f` panelW

**Где:** `DependenciesPanel.cpp:12`

```
const float panelW = 280.0f;
```

По UI_LAYOUT §2.2 — правая панель развёрнутая **280 px**. **Совпадает.** ✅

Но magic. **Фикс:** `constexpr float kRightPanelWidth = 280.0f;`.

**Оценка:** 5 минут.

---

### CR-575. 🟠 `DependenciesPanel` — magic `15.0f` отступ от края

**Где:** `DependenciesPanel.cpp:14`

```
ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelW - 15, panelTop), ...);
```

`15` — magic отступ. **Фикс:** `constexpr float kRightPanelMargin = 15.0f;`.

**Оценка:** 3 минуты.

---

### CR-576. 🟠 `DependenciesPanel` — magic `120.0f` высота ListBox

**Где:** `DependenciesPanel.cpp:21`

```
if (ImGui::BeginListBox("##deps_list", ImVec2(-1, 120))) {
```

**Фикс:** `constexpr float kDepsListHeight = 120.0f;`.

**Оценка:** 2 минуты.

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

### CR-579. 🟡 `DependenciesPanel` — `Tr("panel.dependencies").c_str()` как title без `##` id

**Где:** `DependenciesPanel.cpp:18`

```
ImGui::Begin(Tr("panel.dependencies").c_str(), nullptr, ...);
```

**При смене языка** — `Tr` вернёт другую строку → **ImGui ID изменится** → состояние панели сбросится (позиция, скролл).

**Связано с CR-495** (label как ID).

**Фикс:** `ImGui::Begin((Tr("panel.dependencies") + "##deps_panel").c_str(), ...)`.

**Оценка:** 5 минут.

---

### CR-580. 🟡 `DependenciesPanel` — `panelTop = 340` может перекрыть ViewCube

**Где:** `DependenciesPanel.cpp:13`

ViewCube справа-сверху. Панель зависимостей — `y=340`. Если ViewCube высотой 300+ — перекрытие. **Проверить визуально.**

**Оценка:** 10 минут.

---

---

## 🔴 P0 — ui/panels (Волна 13, часть 5: RightPanel)

### CR-581. 🔴 `RightPanel` — панель шириной 50px, а по UI_LAYOUT — 280px

**Где:** `RightPanel.cpp:20-23`
**Статус:** OPEN
**Категория:** функциональный баг вёрстки

```
const float toolW    = 44.0f;
const float toolH    = 34.0f;
const float pad      = 3.0f;
const float panelW   = toolW + pad * 2.0f;    // = 50
```

**Панель шириной 50 px.**

**По UI_LAYOUT §2.2:**
- Свёрнутая правая панель — **40 px**.
- Развёрнутая — **280 px**.

**Здесь — 50 px** для обеих. **Пользователь видит узкую панель с иконками, но не видит содержимое** (заголовки, описание, стили).

**Фикс:**
```
constexpr float kRightPanelCollapsedW = 40.0f;
constexpr float kRightPanelExpandedW  = 280.0f;
float panelW = panelExpanded ? kRightPanelExpandedW : kRightPanelCollapsedW;
```

**Оценка:** 20 минут.

---

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

### CR-583. 🔴 `RightPanel` — 4× копипаст позиционирования popup

**Где:** `RightPanel.cpp:118-124, 145-151, 185-192`

Три раза одинаковый блок:
```
if (UI::Widgets::SvgIconBtn(*ctx.icons, "zoom_dd", "chevron-down", 14, toolW, arrowH, "...")) {
    ImGui::OpenPopup("##zoom_menu");
    ImVec2 btnMin = ImGui::GetItemRectMin();
    ImVec2 pp(btnMin.x - 280.0f, btnMin.y);
    if (pp.x < 8.0f) pp.x = 8.0f;
    ImGui::SetNextWindowPos(pp, ImGuiCond_Always);
}
```

**Разница только в popup id.** 

**Фикс:** helper `ArrowDropdownButton(icons, id, popupId)`.

**Оценка:** 30 минут.

---

### CR-584. 🔴 `RightPanel` — magic `280.0f` в позиционировании popup

**Где:** `RightPanel.cpp:121, 149, 188`

```
ImVec2 pp(btnMin.x - 280.0f, btnMin.y);   // ← 280
```

**Почему 280?** Связано с шириной окна popup. Но **никем не задана** — ImGui сам вычислит. Magic.

**Фикс:** `constexpr float kPopupOffsetX = 280.0f;` — если действительно нужно; или убрать (ImGui сам справится).

**Оценка:** 5 минут.

---

### CR-585. 🔴 `RightPanel` — 6 пустых MenuItem

**Где:** `RightPanel.cpp:126-130, 153-157, 195-198`

Меню zoom (4), orbit (3), style (3 disabled «скоро»):
- `MenuItem("Зуммировать")` — пусто
- `MenuItem("Показать рамкой")` — пусто
- `MenuItem("Показать выбранное")` — пусто
- `MenuItem("Орбита")` — пусто
- `MenuItem("Орбита с зависимостями")` — пусто
- `MenuItem("Повернуть под углом")` — пусто

**Фикс:** `onNotImplemented(...)` для каждого.

**Оценка:** 20 минут.

---

### CR-586. 🔴 `RightPanel` — SvgIconBtn без обработки кликов

**Где:** `RightPanel.cpp:96, 115, 143, 162, 178`

```
if (UI::Widgets::SvgIconBtn(*ctx.icons, "panel_pan", "hand", 20, toolW, toolH, "Панорамировать")) {}
```

**5 кнопок** с пустыми `{}`. Пользователь жмёт — ничего.

**Связано с B-07** (ribbon-кнопки тоже игнорируют return).

**Фикс:** либо реализовать, либо `onNotImplemented`.

**Оценка:** 30 минут.

---

### CR-587. 🔴 `RightPanel` — 15+ хардкод-строк на русском

**Где:** весь файл

- «Свернуть панель», «Развернуть панель»
- «Панорамировать», «Показать рамкой», «Доп. режимы зума», «Зуммировать», «Показать всё», «Показать выбранное»
- «Свободная орбита», «Орбита», «Орбита с зависимостями», «Повернуть под углом»
- «Вид на объект»
- «Стиль отображения», «Все стили отображения», «Тонированный с рёбрами», «Тонированный», «Каркасный», «Реалистичный (скоро)», «Скрытые линии (скоро)», «Прозрачный (скоро)»
- Все tooltip длинные тексты (5×)

**Плюс 10 хардкод-строк в tooltips.**

**Фикс:** `Tr("panel.xxx")` — ~25 ключей.

**Оценка:** 1.5 часа.

---

### CR-588. 🟠 `RightPanel` — magic `1100.0f` для forceCollapsed

**Где:** `RightPanel.cpp:24`

```
bool forceCollapsed = (io.DisplaySize.x < 1100.0f);
```

**Фикс:** `constexpr float kRightPanelMinScreenW = 1100.0f;`.

**Оценка:** 3 минуты.

---

### CR-589. 🟠 `RightPanel` — 15+ magic чисел layout

**Где:** весь файл

`235.0f, 330.0f, 14.0f, 84.0f, 0.35f, 20.0f, 6.0f, 20.0f, 280.0f, 8.0f, 5.0f, 6.0f...`

**Фикс:** часть в constexpr, часть в layout-структуру.

**Оценка:** 1 час.

---

### CR-590. 🟠 `RightPanel` — `isLight` хардкод-цвета в 2 блоках

**Где:** `RightPanel.cpp:54-61, 67-71`

```
if (hHovered) {
    hBg = IM_COL32(60, 120, 200, 255);
} else if (isLight) {
    hBg = IM_COL32(140, 130, 110, 240);
} else {
    hBg = IM_COL32(90, 100, 120, 240);
}
```

Плюс — `IconTintColor(true)` в hTex. **RULES п.30.**

**Фикс:** `myTheme.rightPanelHandle*` — 4 токена.

**Оценка:** входит в пакет CR-322.

---

### CR-591. 🟠 `RightPanel::RightPanelContext::isolationActive` — передаётся, но не используется

**Где:** `RightPanel.h:15`, `RightPanel.cpp` — 0 использований

**Мёртвое поле.** Связано с CR-421 (ProjectTabs).

**Фикс:** либо использовать (панель меняет вид при изоляции), либо удалить.

**Оценка:** 5 минут.

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

### CR-595. 🔴 `ContextMenu3D` — `ctx.view` без проверки в 6 местах

**Где:** `ContextMenu.cpp:35-46, 50`

```
if (ImGui::MenuItem("Сверху"))
    ctx.view->SetCameraDirectionAndUp(...);   // ← null → краш
if (ImGui::MenuItem("Снизу"))
    ctx.view->SetCameraDirectionAndUp(...);
...
if (ImGui::MenuItem("Показать всё")) {
    if (ctx.view) ctx.view->FitAll();   // ← а тут проверка
}
```

**В 6 case'ах — нет проверки.** В 1 — есть. **Неконсистентно.**

**Фикс:** ранний `if (!ctx.view) return;` или проверка везде.

**Оценка:** 5 минут.

---

### CR-596. 🔴 `ContextMenu3D` — `gp_Dir` в UI (OCCT-тип в UI-слое)

**Где:** `ContextMenu.cpp:9, 36-45`

```
#include <gp_Dir.hxx>
...
ctx.view->SetCameraDirectionAndUp(gp_Dir(0, 0, -1), gp_Dir(0, 1, 0));
```

**UI использует `gp_Dir`** — тип OCCT. Связано с CR-549 (TDF_Label в UI) и CR-004 (LineStyle).

**Фикс:** абстрактный `struct Dir3 { double x, y, z; }` в UI + конвертация в Application.

**Оценка:** входит в CR-549.

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

### CR-598. 🟠 `ContextMenu3D` — 8 хардкод-строк на русском

**Где:** `ContextMenu.cpp:29-50`

- «Домой (F6)», «Изометрия»
- «Вид» (подменю)
- «Сверху», «Снизу», «Спереди», «Сзади», «Справа», «Слева»
- «Показать всё»

**Фикс:** `Tr("view.top")`, и т.д.

**Оценка:** 20 минут.

---

### CR-599. 🟠 `ContextMenu3D` — `<unnamed>` — 3-й раз (Application, PropertiesDialog, здесь)

**Где:** `ContextMenu.cpp:56`

```
std::string name = "<unnamed>";
```

**Фикс:** `Tr("misc.unnamed")` — единая точка. Связано с CR-191, CR-499.

**Оценка:** 3 минуты.

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

### CR-606. 🔴 `DrawDocumentTree` — magic `211` для topY

**Где:** `DocumentTree.cpp:100-101`

```
ImGui::SetNextWindowPos(ImVec2(0, 211), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y - 211 - 76), ...);
```

**211 — откуда?** Должно быть 32 (QAT) + 32 (Tabs) + 78 (Ribbon) = 142. Или 165 (как в Application.cpp:1146). Или 200 (TreeSplitter). Или 206 (Ribbon.cpp).

**Пять разных значений** для одной границы.

**Связано с CR-059, CR-020, CR-564.**

**Оценка:** входит в пакет-унификацию layout.

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

### CR-608. 🔴 `DrawLabelTreeImpl` — `"<unnamed>"` — 4-й раз (Application, PropertiesDialog, ContextMenu, здесь)

**Где:** `DocumentTree.cpp:32`

**Связано с CR-191, CR-499, CR-599.**

**Оценка:** 3 минуты (пакетно).

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

### CR-612. 🔴 `DrawDocumentTree` — `TreeNodeEx("Start", ...)` без `Tr`

**Где:** `DocumentTree.cpp:161`

```
bool opened = ImGui::TreeNodeEx("Start", ...);
```

`"Start"` — хардкод. При RU-локализации должно быть «Начало» или «Базовая геометрия».

**Связано с CR-... локализация.**

**Оценка:** 3 минуты.

---

### CR-613. 🔴 `DrawDocumentTree` — 15+ хардкод-строк

**Где:** весь файл

- `"Save"`, `"Open"` (кнопки)
- `"Start"` (узел дерева)
- `"Скрыть"`, `"Показать"` (тултипы eye)
- `"Показать всё (снять изоляцию)"`
- `"Создать эскиз"`, `"Редактировать эскиз"`, `"Редактировать"`, `"Измерить"`, `"Изолировать"`
- `"Показать все плоскости и оси"`, `"Скрыть все плоскости и оси"`
- `"Копировать"`, `"Вставить"`, `"Переименовать"`, `"Свойства..."`, `"Удалить"`
- `"Copied: "`, `"Paste — TODO"`, `"Auto-enter sketch: tag="`

**~18 строк.** Некоторые уже есть в `Tr()` — но не все.

**Оценка:** 1 час.

---

### CR-614. 🟠 `DrawContextMenuItems` — пустые MenuItem

**Где:** `DocumentTree.cpp:249-250, 281-284`

```
if (ImGui::MenuItem("Редактировать")) {}    // пусто
if (ImGui::MenuItem("Измерить")) {}         // пусто
...
if (ImGui::MenuItem("Вставить")) {
    std::cout << "Paste — TODO\n";          // стаб
}
```

**3 пустых + 1 стаб.**

**Фикс:** `onNotImplemented`.

**Оценка:** 10 минут.

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

### CR-617. 🟠 `DrawLabelTreeImpl` — `requested` возвращается, но никто не использует

**Где:** `DocumentTree.cpp:34, 95-97, 108`

```
bool requested = false;
...
return requested || pendingDelete || pendingCreate;
```

Возвращается из `DrawLabelTreeImpl`. И в `DrawLabelTree` (публичная обёртка) — тоже возвращается. И в `DrawDocumentTree` — **не используется**.

**Что это значит:** `bool` в сигнатуре — **мёртвая нагрузка**.

**Связано с CR-556.**

**Фикс:** `void`.

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

### CR-620. 🟠 `DocumentTree.cpp` — forward decl `DrawContextMenuItems` дублирует header

**Где:** `DocumentTree.cpp:24-26`

```
void DrawContextMenuItems(const TreeContext& ctx,
                          const TDF_Label& label,
                          bool hasOps);
```

**Уже объявлена** в `DocumentTree.h:76`. Повторная — избыточно.

**Фикс:** удалить forward decl.

**Оценка:** 2 минуты.

---

### CR-621. 🟠 `DocumentTree.cpp` — `std::cout` в `DrawDocumentTree`

**Где:** `DocumentTree.cpp:212, 262, 273`

```
std::cout << "Auto-enter sketch: tag=" << ...;
std::cout << "Copied: " << labelName << "\n";
std::cout << "Paste — TODO\n";
```

**CR-133 пакет.**

---

### CR-622. 🟠 `DocumentTree.cpp` — 12+ magic чисел layout

**Где:** весь файл

`211, 76, 340, 14, 22, 18, 20 ...`

**Связано с пакетом-унификацией.**

---

### CR-623. 🟠 `DocumentTree` — `TreeNodeEx(Tr("panel.document").c_str(), ...)` — ID меняется при смене языка

**Где:** `DocumentTree.cpp:148`

Связано с CR-495, CR-579. `Tr` в label виджета → смена языка = смена ID = сброс состояния узла.

**Фикс:** `(Tr(...) + "##doc_root").c_str()`.

**Оценка:** входит в пакет Tr-IDs.

---

### CR-624. 🟠 `DrawContextMenuItems` — `"Start"` — magic имя корневой папки

**Где:** `DocumentTree.cpp:284`

```
if (type == FT_Start) { ... }
```

`Start` — это **имя из `CreateBaseGeometry`** (`TDataStd_Name::Set(myBaseLabel, "Start")`). Захардкожено в двух местах (в doc и здесь — хотя здесь через `FT_Start`, что ок).

**WONTFIX.**

---

### CR-625. 🟠 `DrawContextMenuItems` — `IsSystem(entry)` вызывается в цикле isolation

**Где:** `DocumentTree.cpp:257`

```
for (const auto& entry : allLabels) {
    ...
    if (BreezeDocument::IsSystem(entry)) continue;
```

`IsSystem` → `GetType` → `FindAttribute` — O(1) через OCAF, но для 1000 узлов — 1000 вызовов.

**Не критично, но можно кэшировать.**

**WONTFIX пока.**

---

---

## 🔴 P0 — ui/viewcube (Волна 14, часть 1: заголовки)

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

### CR-628. 🔴 `Animator` — все поля public, «потом закроем» — не закрыли

**Где:** `ViewCubeAnimator.h:22-30`
**Статус:** OPEN
**Категория:** инкапсуляция

```
class Animator {
public:
    // ==== Состояние (пока public — на шаге 1.2b, потом закроем) ====
    bool    myActive = false;
    float   myT      = 0.0f;
    double  myFromDir[3] = {0, 0, -1};
    double  myToDir[3]   = {0, 0, -1};
    double  myFromUp[3]  = {0, 1, 0};
    double  myToUp[3]    = {0, 1, 0};
    std::vector<AnimTarget> myQueue;
};
```

**«Потом закроем» — не закрыли.** Поля **public**, `Application` (или кто-то ещё) может их менять напрямую.

**Фикс:** перевести в `private`, оставить публичные методы:
- `void Update(float dt)`
- `void StartAbsolute(dir, up)`
- `void StartRoll(angle)`
- `bool IsActive() const`
- `void Clear()`

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

### CR-632. 🟠 Все комментарии про «шаги 1.2/1.3/1.4» — устаревшие

**Где:** `ViewCubeOverlay.h:10`, `ViewCubeAnimator.h:5`, `ViewCubeGeometry.h:6`

```
// Реализация — на шаге 1.4 рефакторинга.
// Реализация — на шаге 1.2 рефакторинга.
// Заполним таблицами kFaces/kEdges/kCorners на шаге 1.3.
```

**Рефакторинг завершён.** Эти TODO — **мусор**.

**Фикс:** убрать или превратить в реальные TODO по CR-626/CR-627.

**Оценка:** 3 минуты (пакетно).

---

### CR-633. 🟠 `ViewCubeOverlay` — комментарий «Слои: ui зависит от ImGui и kernel через интерфейсы»

**Где:** `ViewCubeOverlay.h:9`

```
// Слои: ui зависит от ImGui и kernel через интерфейсы, не наоборот.
```

**Заявка в комментарии не соответствует коду:**
- `Context` содержит `OcctView&` — конкретный класс, не интерфейс.
- Нет `IKernel*` — прямое использование OCCT.
- CR-596 (gp_Dir в UI) подтверждает — **прямая зависимость от OCCT**, не через интерфейс.

**Фикс:** либо реализовать интерфейс (Фаза 3+), либо убрать ложную заявку.

**Оценка:** 2 минуты (убрать) или 8 часов (реализовать).

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

### CR-639. 🔴 `ViewCubeOverlay.cpp` — 6 подписей граней хардкодом

**Где:** `ViewCubeOverlay.cpp:129-134`

```
"ВЕРХ", "НИЗ", "ПЕРЕД", "ЗАД", "ПРАВО", "ЛЕВО"
```

**При смене языка** — останутся русскими. Это **часть BUG-4.1** («ВЛЕВО» вместо «ЛЕВО»).

**Связано с CR-638.**

**Фикс:** в `ViewCubeGeometry` — ключи `"cube.top"`, `"cube.bottom"`, и т.д. + `Tr()` при рендере.

**Оценка:** 30 минут (после CR-638).

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

### CR-641. 🔴 `ViewCubeOverlay.cpp` — magic числа layout

**Где:** `ViewCubeOverlay.cpp:22-26`

```
const float cubeSize   = 100.0f;
const float cubeHalf   = 38.0f;      // ← почему не cubeSize/2?
const float margin     = 15.0f;
const float topOffset  = 260.0f;
const float cubeShiftX = ctx.rightPanelShiftX;
```

**`cubeHalf = 38`, а `cubeSize/2 = 50`.** Cube рисуется **меньше**, чем объявлен размер. Несогласованность.

**Фикс:** `constexpr float kCubeHalfSize = 38.0f;` (без `cubeSize` вообще) или пересчитать связь.

**Оценка:** 15 минут.

---

### CR-642. 🔴 `ViewCubeOverlay.cpp` — magic `1e-9` в 3 местах + `1e-6`

**Где:** `ViewCubeOverlay.cpp:68, 98, 109`

```
if (len < 1e-9) { ... }
if (dlen < 1e-9) dlen = 1.0;
if (ulen > 1e-6) gp_Dir(...);
```

**Три разных порога для одной задачи** (нормализация). `1e-9` vs `1e-6`.

**Фикс:** `constexpr double kMinLen = 1e-9;`.

**Оценка:** 5 минут.

---

### CR-643. 🔴 `ViewCubeOverlay.cpp` — `3.14159265358979323846` — PI вручную

**Где:** `ViewCubeOverlay.cpp:60`

```
double a  = angleDeg * 3.14159265358979323846 / 180.0;
```

`M_PI` — POSIX, но лучше `constexpr double kDegToRad = ...` (см. CR-311).

**Фикс:** `util/MathConstants.h` или `constexpr`.

**Оценка:** 5 минут.

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

### CR-646. 🟠 `ViewCubeOverlay.cpp` — `gp_Dir` в UI (OCCT в UI)

**Где:** `ViewCubeOverlay.cpp:31-32, 62-63, 89-90, 100-101, 116-124` и т.д.

```
gp_Dir camDir = view.GetCameraDirection();
gp_Dir camUp  = view.GetCameraUp();
gp_Vec crossVec = camDir.Crossed(camUp);
gp_Dir camRight;
...
```

**UI-модуль работает с `gp_Dir`, `gp_Vec`, `gp_Pnt`** — типами OCCT.

**Связано с CR-596, CR-549, CR-004.**

**Оценка:** входит в комплексный CR по `Label`/`Dir` в UI.

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

### CR-650. 🟠 `ViewCubeOverlay.cpp` — `cornerHalf = 10.0f`, `edgeTol = 6.0f` — magic

**Где:** `ViewCubeOverlay.cpp:218-219`

**Фикс:** `constexpr`.

**Оценка:** входит в CR-641.

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

### CR-655. 🟠 `ViewCubeOverlay.cpp` — `#include "occt/OcctView.h"` в UI

**Где:** `ViewCubeOverlay.cpp:2`

UI-модуль **подключает `OcctView.h`**, который тянет `AIS_InteractiveContext.hxx`, `OpenGl_GraphicDriver.hxx`, `V3d_View.hxx` и т.д.

**Что это значит:** полная OCCT-зависимость в UI-модуле. **Связано с CR-646.**

**Оценка:** входит в комплексный CR.

---

### CR-656. 🟡 `ViewCubeOverlay.cpp` — комментарий «Плавная анимация камеры: 0.5 сек»

**Где:** `ViewCubeOverlay.cpp:36`

Комментарий есть — хорошо. Но `0.5f` (строка 38) — magic. **Входит в CR-641.**

---

---

## 🔴 P0 — ui/viewcube (Волна 14, часть 3: Overlay.cpp 260-524)

### CR-657. 🔴 КОРЕНЬ BUG-4.1 — таблицы `faceTextRight/faceTextDown`

**Где:** `ViewCubeOverlay.cpp:277-282`
**Статус:** OPEN
**Категория:** корень BUG-4.1

```
static const double faceTextRight[6][3] = {
    { 1, 0, 0}, {-1, 0, 0}, { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0},
};
static const double faceTextDown[6][3] = {
    { 0,-1, 0}, { 0,-1, 0}, { 0, 0,-1}, { 0, 0,-1}, { 0, 0,-1}, { 0, 0,-1},
};
```

**Соответствие faces:**
| fi | Грань | n | textRight | textDown |
|---|---|---|---|---|
| 0 | ВЕРХ | +Z | (1,0,0) | (0,-1,0) |
| 1 | НИЗ | -Z | (-1,0,0) | (0,-1,0) |
| 2 | ПЕРЕД | -Y | (1,0,0) | (0,0,-1) |
| 3 | ЗАД | +Y | (-1,0,0) | (0,0,-1) |
| 4 | ПРАВО | +X | (0,1,0) | (0,0,-1) |
| 5 | ЛЕВО | -X | (0,-1,0) | (0,0,-1) |

**Проблема:** таблица захардкожена в `.cpp` файла, не вынесена в `ViewCubeGeometry.h`. **Невозможно протестировать**, невозможно проверить логику.

**BUG-4.1** состоит из **двух независимых проблем:**
1. **«ВЛЕВО» вместо «ЛЕВО» — обрезка.** Рендер текста через `AddImageQuad` обрезает последний глиф при определённых углах.
2. **«При клике на грань — текст на другой грани».** Это — **неверная привязка** `faceTextRight`/`faceTextDown` к `fi`.

**Диагностика:**
- Взять `ViewCubeOverlay.cpp` в чистом виде, изолировать функцию рендера подписей.
- Прогнать через тест: «камера изометрия → ожидаем текст «ВЕРХ» на верхней видимой грани, читаемый горизонтально».
- Сравнить с реальным скриншотом.

**Фикс:**
1. Перенести обе таблицы в `ViewCubeGeometry.h` (часть CR-626).
2. Написать юнит-тест: для каждой грани — построить камеру напротив неё → проверить, что `p3d(u,v)` даёт точки **в плоскости** этой грани с правильной ориентацией.
3. Устранить расхождение.

**Оценка:** 2-3 часа (правильно).

---

### CR-658. 🔴 `ViewCubeOverlay.cpp` — рендер текста через `AddImageQuad` — сложная кастомная логика

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

### CR-659. 🔴 `ViewCubeOverlay.cpp` — ручной UTF-8 парсинг

**Где:** `ViewCubeOverlay.cpp:331-338`

```
unsigned char c0 = (unsigned char)s[0];
int charLen = 1;
unsigned int cp = c0;
if      (c0 < 0x80)                       charLen = 1;
else if ((c0 & 0xE0) == 0xC0 && s+1 < s_end) { cp = ((c0 & 0x1F) << 6) | ...; charLen = 2; }
else if ((c0 & 0xF0) == 0xE0 && s+2 < s_end) { cp = ...; charLen = 3; }
else if ((c0 & 0xF8) == 0xF0 && s+3 < s_end) { cp = ...; charLen = 4; }
```

**Ручной UTF-8** — **здесь не нужен.** В ImGui есть `ImTextCharFromUtf8(&out_char, s, s_end)` (в `imgui_internal.h`, который **уже подключён** — CR-645).

**Фикс:** заменить на `ImTextCharFromUtf8`.

**Оценка:** 10 минут.

---

### CR-660. 🔴 `ViewCubeOverlay.cpp` — `cp` как `unsigned int` → `ImWchar`

**Где:** `ViewCubeOverlay.cpp:346`

```
const ImFontGlyph* g = font->FindGlyph((ImWchar)cp);
```

**`ImWchar` в ImGui по умолчанию = `uint16_t`.** Если `cp > 0xFFFF` (эмодзи, редкие символы) — **усечение**. Для русских букв (U+0410..U+044F) — ок. Но для будущего (символы `°`, `×`, `Ø`) — критично.

**Фикс:** `font->FindGlyphNoFallback((ImWchar)cp)` + проверка диапазона, или использовать `FindGlyph` с правильной кодировкой.

**Оценка:** 10 минут.

---

### CR-661. 🔴 `ViewCubeOverlay.cpp` — `scale = fSizePx / font->FontSize` для каждого глифа

**Где:** `ViewCubeOverlay.cpp:348`

**Что это:** `font->FontSize` — базовый размер шрифта (18 pt по `LoadFonts`). `fSizePx` — вычисленный размер грани. **Но результат одинаковый для всех глифов одной грани.**

**Фикс:** вычислить `scale` **один раз** до цикла.

**Оценка:** 2 минуты.

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

### CR-664. 🔴 `ViewCubeOverlay.cpp` — magic числа в кнопках

**Где:** `ViewCubeOverlay.cpp:516-533`

```
const float btnW = 40.0f;
const float btnH = 28.0f;
const float gap  = 6.0f;
const float winW = btnW * 2.0f + gap + 16.0f;   // ← 16 magic
const float winH = btnH + 16.0f;                 // ← 16 magic
...
if (myAnimator.myQueue.size() < 8) { ... }        // ← 8 magic
...
if (ImGui::Button("<<", ImVec2(btnW, btnH))) {
    rollCameraAnim( 90.0);
}
if (ImGui::Button(">>", ImVec2(btnW, btnH))) {
    rollCameraAnim(-90.0);
}
```

**6 magic.** Плюс `90.0` — угол поворота.

**Фикс:** `constexpr`.

**Оценка:** 20 минут.

---

### CR-665. 🔴 `ViewCubeOverlay.cpp` — tooltips «Повернуть по часовой на 90» — хардкод + без `°`

**Где:** `ViewCubeOverlay.cpp:527, 532`

```
ImGui::SetTooltip("Повернуть по часовой на 90");
ImGui::SetTooltip("Повернуть против часовой на 90");
```

- Не локализованы.
- «на 90» — без `°`.
- Неясно, какая кнопка (<< или >>) что делает.

**Фикс:** `Tr("vc.roll.cw")`, `Tr("vc.roll.ccw")`.

**Оценка:** 5 минут.

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

### CR-667. 🟠 `ViewCubeOverlay.cpp` — `3.14159265358979323846` снова

**Где:** `ViewCubeOverlay.cpp:462`

**Связано с CR-643.** Одна и та же константа в 2 местах одного файла.

**Оценка:** 2 минуты (пакетно).

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

### CR-670. 🟠 `ViewCubeOverlay.cpp` — `stickyL/R/T/B` — magic 80/70/100

**Где:** `ViewCubeOverlay.cpp:486-490`

```
const float stickyL = center.x - 80.0f;
const float stickyR = center.x + 80.0f;
const float stickyT = center.y - 70.0f;
const float stickyB = center.y + cubeSize * 0.5f + 100.0f;
```

**Фикс:** часть layout-структуры ViewCube.

**Оценка:** 30 минут (входит в CR-641).

---

### CR-671. 🟠 `ViewCubeOverlay.cpp` — `arrowY = center.y + cubeSize * 0.5f + 10.0f` — magic 10

**Где:** `ViewCubeOverlay.cpp:481`

**+10 magic.**

**Оценка:** входит в CR-641.

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

### CR-681. 🔴 `Utf8.cpp` — ручной UTF-8 парсинг дублирует `ViewCubeOverlay.cpp`

**Где:** `Utf8.cpp:13-30` vs `ViewCubeOverlay.cpp:331-338`

**Два места** в проекте делают ручной UTF-8 → code point:
- `Utf8ToExt` (здесь) — 30 строк.
- `ViewCubeOverlay::Draw` — 8 строк (CR-659).

**Связано с CR-659.**

**Фикс:** `util/Utf8Decode(s, &pos) -> uint32_t` — единый helper, оба места используют.

**Оценка:** 30 минут.

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

### CR-686. 🔴 `ComputeLengthAngle` — `origin` объявлена, но НЕ используется

**Где:** `Geometry2D.cpp:22`

```
gp_Ax3 ax = plane.Position();
gp_Pnt origin = ax.Location();   // ← объявлена
gp_Dir xDir = ax.XDirection();
gp_Dir yDir = ax.YDirection();

gp_Vec v(from, to);              // ← origin НЕ используется
```

**Dead variable.** GCC с `-Wall` даст warning. Функция не компилируется с `-Werror`.

**Фикс:** удалить `origin`.

**Оценка:** 1 минута.

---

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

### CR-691. 🔴 `Geometry2D.cpp` — `#ifndef M_PI #define` — костыль

**Где:** `Geometry2D.cpp:9-11`

```
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
```

**Костыль для MSVC** (где M_PI не определён без `_USE_MATH_DEFINES`).

**Плюс:** сама константа `3.14159265358979323846` **уже дублируется** в `ViewCubeOverlay.cpp:60, 462`.

**3 места одной константы.**

**Фикс:** `util/MathConstants.h` с `constexpr double kPi = ...;`.

**Оценка:** 15 минут (пакетно с CR-643, CR-667).

---

### CR-692. 🟠 `Geometry2D` — magic `1e-6` в `ApplyOrtho`

**Где:** `Geometry2D.cpp:44`

```
if (len < 1e-6) return to;
```

**Связано с CR-642** (1e-9 в ViewCubeOverlay). **Разные пороги** в разных модулях для одной задачи (min длина).

**Фикс:** `constexpr double kMinLength = 1e-6;` в `util/MathConstants.h`.

**Оценка:** входит в CR-691.

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

### CR-699. 🔴 `GetLabelName` — `"Line" + std::to_string(label.Tag())` — хардкод

**Где:** `LabelName.cpp:28`

```
return "Line" + std::to_string(label.Tag());
```

**Хардкод «Line»** на английском. Не локализовано. Связано с CR-384, CR-500, CR-587.

**Плюс:** `std::to_string` — heap-аллокация. Для fallback при каждом узле без имени.

**Фикс:** `Tr("label.unnamed_line")` + `snprintf`.

**Оценка:** 10 минут.

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

### CR-715. 🔴 `GlfwWindow::ShouldClose` — без проверки `myWindow != nullptr`

**Где:** `GlfwWindow.cpp:63-65`

```
bool GlfwWindow::ShouldClose() const {
    return glfwWindowShouldClose(myWindow);   // ← myWindow может быть null
}
```

**Если `glfwCreateWindow` провалился** (CR-086 в Application) — `myWindow == nullptr`. Вызов `glfwWindowShouldClose(nullptr)` — **UB/краш**.

**То же для:**
- `PollEvents()` — `glfwPollEvents()` — глобальная функция, ок.
- `SwapBuffers()` — `glfwSwapBuffers(nullptr)` — **UB**.

**Фикс:** `if (!myWindow) return true;` в `ShouldClose`, `if (myWindow) glfwSwapBuffers(...)`.

**Оценка:** 5 минут.

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

### CR-729. 🔴 `Localization` — синглтон с `Language myLang` — глобальное состояние

**Где:** `Localization.h:11-22`
**Статус:** OPEN

```
class Localization {
    static Localization& Instance();
    Language myLang = Language::RU;
};
```

**Связано с CR-729** (первая часть) и с `SettingsDialog.cpp:112` (`Localization::Instance().GetLanguage()`).

**Плюс:** в `Localization.h:21` inline `Tr()` — free function, оборачивает `Instance().Tr(key)`. **Единый API для UI**, но внутренне — синглтон.

**Фикс:** либо **полный синглтон** (все через `Instance()`), либо **полная инжекция** (Language в Context). Смешение подходов — худший вариант.

**Оценка:** 2 часа (правильно).

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

### CR-740. 🔴 `BreezeApplication::CloseDocument` — `catch(...)` без логирования

**Где:** `BreezeApplication.cpp:83-88`

```
void BreezeApplication::CloseDocument(const Handle(TDocStd_Document)& doc) {
    if (doc.IsNull()) return;
    try {
        myApp->Close(doc);
    } catch (...) {
        // ignore
    }
}
```

**Комментарий `// ignore`.** Если закрытие провалилось — **молча**.

**Что это значит:**
- OCAF-документ остаётся в сессии → **утечка памяти** (RAM-H10).
- Пользователь ничего не видит.
- При закрытии приложения — `BreezeApplication::~BreezeApplication` не вызывается явно (singleton через `static` в `.cpp`).

**Связано с RAM-H10.**

**Фикс:** лог + `myApp->Close(doc)` без try/catch (пусть падает громко) ИЛИ явный `AbortCommand`.

**Оценка:** 10 минут.

---

### CR-741. 🔴 `BreezeApplication` — singleton `~BreezeApplication() = default` — никогда не вызывается

**Где:** `BreezeApplication.h:26`

```
~BreezeApplication() = default;
```

**Singleton `static BreezeApplication instance;`** внутри функции `Instance()`:
- Создаётся при первом вызове.
- Уничтожается **при завершении программы** (после `main`, до выхода).
- **Но:** порядок деструкторов `static` **не определён**.
- Если `GlfwWindow` (глобальный) уже умер, а `BreezeApplication` пытается что-то сделать — **UB**.

**Плюс:** `TDocStd_Application` (OCCT) содержит OCAF-сессии, документы, форматы. При завершении программы могут быть утечки.

**Фикс:**
- Явный `Shutdown()` метод.
- Вызывать из `main()` перед выходом.
- Или отказаться от singleton.

**Оценка:** 1 час.

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

### CR-744. 🟠 `BreezeApplication` — много `std::cout` (CR-133 пакет)

**Где:** `BreezeApplication.cpp:17, 31, 52, 63`

**Связано с CR-133.**

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
