# BreezeCAD — Review: P1 / misc

**Записей:** 7

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

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

---

### CR-012. `ImGui::SeparatorEx` + `ImGuiSeparatorFlags_Vertical` — internal API

**Где:** `AnnotateTab.cpp`, `ToolsTab.cpp`, `ManageTab.cpp`
**Статус:** OPEN

`SeparatorEx` — **не** public ImGui API. Может исчезнуть или измениться.

**Фикс:** исчезнет после CR-011.

---

---

### CR-013. `static const char* oneOpt[] = { "tool.dummy" };` — мёртвая переменная

**Где:** `ModelTab.cpp:19`, `AnnotateTab.cpp`, `ToolsTab.cpp`, `ManageTab.cpp`, `SketchTab.cpp`
**Статус:** OPEN

Объявлена, нигде не используется.

**Фикс:** удалить.

**Оценка:** 5 минут.

---

---

### CR-047. `SketchTab.cpp` — тихая деградация при `nullptr`

**Где:** `SketchTab.cpp:205, 219`

```
if (ctx.currentLineStyle) *ctx.currentLineStyle = items[i].style;
```

Если `nullptr` — подсветка работает, но выбор не сохраняется. Пользователь в недоумении. **Надо assert или лог.**

---

---

### CR-054. `Annotate/Tools/Manage.cpp` — цвета текста не через тему

**Где:** все три файла
`ImGui::GetStyleColorVec4(ImGuiCol_Text)` — базовый ImGui, не токен проекта.

**Фикс:** после CR-011 автоматически уйдёт (там будет `GroupEnd` с `Tr`).

---

---

### CR-123. `std::cout` / `std::cerr` — BUG-A4

**Где:** 15+ мест в этом блоке.
Кириллица → CP866-кракозябры в MSYS2.

**Фикс:** единый логгер или `setlocale`.

---

---

