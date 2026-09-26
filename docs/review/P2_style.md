# P2 — СТИЛЬ — часть 1 из 1

**Записей в части:** 41

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

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


