# BreezeCAD — Review: P0 / panels

**Записей:** 24

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

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

---

### CR-553. 🟠 `TreeContext::onShowAllRespectingDefaults` — имя длиннее 30 символов

**Где:** `DocumentTree.h:62`

```
std::function<void()>                 onShowAllRespectingDefaults;
```

**Длинное имя** — но **семантически правильное** (соответствует `UI::Panels::ShowAllRespectingDefaults` в Visibility.h). Ок.

**WONTFIX.**

---

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

---

### CR-555. ✅ `TreeSplitter.h` — чисто

10 строк, одна функция, никаких проблем.

**Статус:** NOT A BUG.

---

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

---

### CR-592. 🟠 `RightPanel` — `ctx.tooltip->Draw()` вызывается без проверки null

**Где:** `RightPanel.cpp:99, 117, 145, 164, 180`

```
if (ctx.tooltip) ctx.tooltip->Draw(...);
```

**Проверка есть — ок.** WONTFIX.

---

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

---

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

---

### CR-601. 🟠 `ContextMenu3D` — `hasOps` вычисляется, но не передаётся дальше

**Где:** `ContextMenu.cpp:63-64`

```
bool hasOps = ctx.doc ? ctx.doc->Has3DOperations() : false;
DrawContextMenuItems(ctx, sel, hasOps);
```

**Ок** — передаётся. WONTFIX.

---

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

---

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

---

### CR-619. 🟠 `DrawContextMenuItems` — `hasOps` передаётся, но используется только в одном месте

**Где:** `DocumentTree.cpp:295`

```
bool curVisible = (it != ctx.visibility->end()) ? it->second : !hasOps;
```

**В остальных местах `hasOps` не используется.** Функция могла бы обойтись без него или использовать чаще.

**Отложено.**

---

---

### CR-624. 🟠 `DrawContextMenuItems` — `"Start"` — magic имя корневой папки

**Где:** `DocumentTree.cpp:284`

```
if (type == FT_Start) { ... }
```

`Start` — это **имя из `CreateBaseGeometry`** (`TDataStd_Name::Set(myBaseLabel, "Start")`). Захардкожено в двух местах (в doc и здесь — хотя здесь через `FT_Start`, что ок).

**WONTFIX.**

---

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

---

