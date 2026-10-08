# BreezeCAD — Review: P0 / qat

**Записей:** 18

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

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

---

### CR-381. `QatContext` — 11 `std::function` колбэков без default-конструктора

**Где:** `QuickAccessToolbar.h:32-44`

**Проблема:** `std::function` по умолчанию — **пустой**. Если Application забудет задать `onSave` — вызов `.operator()` бросит `std::bad_function_call`.

Сейчас защита — `if (ctx.onSave) ctx.onSave();` в **каждом** месте. Но это **verbosity** + легко забыть.

**Фикс:** либо `template <typename T> void CallIf(const std::function<T>& f, ...)`, либо — оставить как есть, но **проверять единообразно**.

Сейчас — часть колбэков проверяется (`if (ctx.onSave) ...`), часть — **нет** (например `ctx.tooltip->Draw(...)` — `tooltip` не проверяется вообще).

**Оценка:** 20 минут.

---

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

---

### CR-387. Меню формат окна — «Свернуть»/«Развернуть» дублируют системные кнопки

**Где:** `QuickAccessToolbar.cpp:22-36`

В меню лого есть «Свернуть» и «Развернуть». Это **дублирует** системные кнопки окна `[—] [□]` в правом верхнем углу.

**Это ошибка копирования?** Или задумка для Windows-стиля?

Проверить с MikeVerx.

**WONTFIX** пока (уточнить).

---

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

---

### CR-395. `qat_modules` (левая) и `qat_modules` (правая) — **ImGui ID conflict**

**Где:** `QuickAccessToolbar.cpp:104 и 201`

Два `QatIconButton(*ctx.icons, "qat_modules", ...)` — **одинаковый id**.

**ImGui behavior:** `PushID` + одинаковый id → `InvisibleButton` второго не перезаписывает hover первого, но при определённых условиях — **race** в `IsItemHovered()`.

**Связано с CR-383.**

**Оценка:** входит в CR-383.

---

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

---

