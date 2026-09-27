# BreezeCAD — Review: P0 / dialogs

**Записей:** 39
**Часть:** 1/2

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

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

---

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

---

### CR-474. `DeleteConfirmDialog` — нет `onCancelled` колбэка

**Где:** `DeleteConfirmDialog.h:22-27`

Есть `onDeleted`, но **нет `onCancelled`**. Если понадобится логировать отмены или сбрасывать что-то в Application — некуда.

**Фикс:** добавить `std::function<void()> onCancelled;`.

**Оценка:** 5 минут.

---

---

### CR-475. `DeleteConfirmDialog` — `char info[256]` — риск обрезки UTF-8

**Где:** `DeleteConfirmDialog.h:12`

`Application::RequestDelete` использует `std::snprintf` для формирования `info`. Если имя фичи длинное — обрезка **посередине UTF-8** → кракозябры в конце.

**Фикс:** `std::string info;` + `ImGui::TextWrapped("%s", info.c_str())`.

**Оценка:** 15 минут.

---

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

---

### CR-498. `PropertiesDialog` — `"Свойства"` — хардкод (1 строка)

**Где:** `PropertiesDialog.cpp:18`

`ImGui::Begin("Свойства", ...)` — хардкод. **Фикс:** `Tr("dlg.properties.title")`.

**Оценка:** 2 минуты.

---

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

---

### CR-503. `PropertiesDialog` — `Tag() %d` — потенциальное переполнение

**Где:** `PropertiesDialog.cpp:31`

```
ImGui::Text("Тег: %d", ctx.state->pendingLabel.Tag());
```

`Tag()` возвращает `int`. Ок. WONTFIX.

---

---

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

---

### CR-514. Все диалоги — нет `ESC`-обработки через `p_open`

**Где:** все, кроме NotImplementedDialog

`p_open = nullptr` → ESC **не закрывает** модалку. Пользователь жмёт ESC → ничего. **UX-баг** из UI_LAYOUT §1.5 «Всё можно отменить (Ctrl+Z, Esc)».

**Фикс:** везде передавать `&state.show` + `if (state.show) OpenPopup; state.show = false;`.

**Связано с CR-512.**

---

---

### CR-515. Диалоги — нет единой темы оформления

**Где:** все

Цвета кнопок, отступы, размеры кнопок (`ImVec2(120, 0)`), separator-стили — **у каждого диалога свои**. Не используется `ThemeTokens`.

**Связано с CR-322** (расширение токенов).

**Оценка:** входит в пакет CR-322.

---

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 8: DependencyDialog)

---

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

---

### CR-522. `DependencyDialog` — `Util::GetLabelName` — зависимость от `util/LabelName`

**Где:** `DependencyDialog.cpp:4, 46, 67`

UI зависит от `util/LabelName`. `LabelName` — обёртка над `TCollection_AsciiString`. **BUG-A5** (`TCollection_AsciiString` теряет кириллицу). Здесь UI получает потенциально сломанное имя.

**Связано с BUG-A5.**

**Фикс:** сначала BUG-A5.

---

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

---

### CR-538. `SettingsDialog` — нет кнопки «Сбросить до дефолта»

**Где:** весь файл

По UI_LAYOUT — есть, но не критично.

**Отложено.**

---

---

### CR-540. `SettingsDialog` — `BeginChild("##settings_tabs")` + `Selectable` = «свои вкладки»

**Где:** `SettingsDialog.cpp:22-31`

Левая панель — `BeginChild` + `Selectable`. ImGui имеет нативные `BeginTabBar`/`BeginTabItem`. Но **вертикальные табы** (как в Settings) — не штатно. Ок.

**WONTFIX.**

---

---

