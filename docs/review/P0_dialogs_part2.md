# BreezeCAD — Review: P0 / dialogs

**Записей:** 21
**Часть:** 2/2

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

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

---

### CR-543. `SettingsDialog` — нет сохранения настроек в файл

**Где:** весь файл

При закрытии приложения все настройки (кроме темы/языка — **если они сохраняются где-то**) теряются.

**Связано с CR-531.**

**Оценка:** 3 часа (полноценно).

---

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

---

### CR-545. `SettingsDialog` — `Language` enum в `app/Localization.h` — UI зависит от app

**Где:** `SettingsDialog.h:13`

```
#include "app/Localization.h"      // Language
```

UI-заголовок **подключает** `app/Localization.h`. По `ARCHITECTURE §2` — UI **может** зависеть от app (app выше ui), но зависимость от **конкретного заголовка** — хрупко.

**WONTFIX** — норм по слоям.

---

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

---

### CR-552. 🟠 `TreeContext` — 7 колбэков без `default`, без проверок

**Где:** `DocumentTree.h:59-66`

`onSave`, `onOpen`, `onEnterSketch`, `onShowAllRespectingDefaults`, `onRequestDelete`, `onRenameRequest`, `onPropertiesRequest`.

**Всё `std::function`** — могут быть пустыми. Связано с CR-381 (QAT).

**Фикс:** assert или проверки.

**Оценка:** входит в CR-381-пакет.

---

---

### CR-602. 🟠 `ContextMenu3D` — нет `onDelete`, `onRename` для 3D-меню

**Где:** `ContextMenu.cpp` — весь файл

Меню в 3D показывает только «Вид», «Домой», «Изометрия», «Показать всё». **Нет** «Удалить», «Переименовать», «Свойства».

**По UI_LAYOUT §18** — контекстное меню 3D должно содержать операции над объектом.

**Проверить с MikeVerx** — нужно ли добавлять.

**Оценка:** 30 минут (если нужно).

---

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

---

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

---

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

---

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

---

