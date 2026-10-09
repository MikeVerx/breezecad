# BreezeCAD — Review: P0 / widgets

**Записей:** 17

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

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

---

### CR-361. `IconTintColor` — магия `0.45f`

**Где:** `IconTint.cpp:11`

```
if (!enabled) tint.w *= 0.45f;
```

**Фикс:** `constexpr float kDisabledIconAlpha = 0.45f;`.

**Оценка:** 2 минуты.

---

---

### CR-364. `SvgIconBtn` — `#include "imgui.h"` в `.cpp`, но `GLuint` / `GL_TEXTURE_2D`?

**Где:** `SvgButton.cpp`

`GLuint tex = icons.Get(...)` — тип `GLuint`. Нужен `#include <GL/gl.h>`. **Не подключён явно.** Работает через `IconManager.h` (который включает gl.h).

**Хрупко.** **Фикс:** явный include.

**Оценка:** 2 минуты.

---

---

## 🔴 P0 — ui/widgets (Волна 9, часть 4: Tooltip + SelectionRect)

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

---

### CR-370. `Tooltip::Draw` — не проверяет перекрытие с виджетом

**Где:** `Tooltip.cpp:41-67`

UI_LAYOUT §10.2: «Тултип не перекрывает кнопку, к которой относится».

Сейчас — `ImGui::BeginTooltip()` без `SetNextWindowPos` — ImGui сам ставит рядом, но **на малых экранах может перекрыть**. Не критично для MVP.

**WONTFIX** (до Фазы 2).

---

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

---

### CR-372. `Tooltip.cpp` — русский хардкод `"(картинка: %s)"`

**Где:** `Tooltip.cpp:62`

**Фикс:** `Tr("tooltip.image_placeholder")`.

**Оценка:** 3 минуты.

---

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

---

### CR-374. `Tooltip::Draw` — параметры `const std::string&` могут быть dangling

**Где:** `Tooltip.cpp:14-19`

Если вызывающий передаёт `Tr("key")`, и `Tr` возвращает `std::string` **by value** — параметр const& привязан к временному, жив до конца вызова. Ок.

Если **`const std::string&`** возвращает — тоже ок.

Если где-то передаётся **raw char***, приведённый к `std::string` — временный объект, тоже ок (до конца выражения).

**WONTFIX** — контракт безопасен.

---

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

---

