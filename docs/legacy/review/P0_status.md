# BreezeCAD — Review: P0 / status

**Записей:** 12

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

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

---

### CR-411. `StatusBar` — `io.DisplaySize.y - h` без проверки минимума

**Где:** `StatusBar.cpp:18`

Если окно меньше 24 px (маловероятно, но) — статус-бар уедет за границу.

**WONTFIX** (нереально).

---

## 🔴 P0 — ui/status (Волна 10, часть 4: ProjectTabs)

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

---

### CR-421. `ProjectTabs` — не использует `ctx.rightPanelExpanded`

**Где:** `ProjectTabsContext` (нет поля, но QAT/StatusBar его имеют)

Вкладки проектов не сдвигаются с учётом правой панели. При развёрнутой панели — последние табы уезжают под неё.

**Проверить визуально.**

**Оценка:** 10 минут.

---

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

---

### CR-423. `ProjectTabs` — `io.MouseWheel` в hot-path

**Где:** `ProjectTabs.cpp:24`

```
if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0.0f) {
```

Ок — только при активном скролле. **WONTFIX.**

---

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

---

### CR-425. `ProjectTabs` — контекстное меню (`BeginPopupContextItem`) на каждой кнопке

**Где:** `ProjectTabs.cpp:91`

`BeginPopupContextItem("##tab_ctx")` — ок, но `##tab_ctx` — **фиксированное имя** для всех табов. ImGui использует ID-стек (`PushID(i)`), поэтому ок. **WONTFIX.**

---

---

### CR-447. HomeScreen — дублирование «EndChild/PopStyleColor/PopID» в 2 ветках
**Включает (merged):** CR-478, CR-524, CR-530

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

---

