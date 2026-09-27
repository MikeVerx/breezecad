# BreezeCAD — Review: P1 / panels

**Записей:** 11

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-272. `OcctView::SetShapeVisible` — silent fail при отсутствии label

**Где:** `OcctView.cpp:302-317`

Если `label` не найден в `myShapeEntries` — цикл заканчивается, **ничего не делается, никакой диагностики**. Возможны расхождения между `myVisibility` в `Application` и реальным состоянием сцены.

**Фикс:** debug-warning при ненайденной label.

**Оценка:** 5 минут.

---

---

### CR-558. ✅ `Visibility` — закрывает B-01 (эталон)

**Где:** `Visibility.cpp:14-23`
**Статус:** NOT A BUG

Комментарий: «Закрывает B-01 (тройное дублирование логики видимости)». Правильная единая точка.

**Действие:** без изменений.

---

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

---

### CR-561. 🟠 `Visibility.cpp` — `std::cout` (CR-133 пакет)

**Где:** `Visibility.cpp:47`

```
std::cout << "ShowAllRespectingDefaults: restored\n";
```

**Связано с CR-133.**

**Оценка:** входит в пакет CR-133.

---

---

### CR-568. 🟡 `TreeSplitter` — `ImGui::SetNextWindowPos/Size(... ImGuiCond_Always)`

**Где:** `TreeSplitter.cpp:17-18`

Каждый кадр **пересчитывается** позиция. Работает, но чуть избыточно. **WONTFIX.**

---

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

---

### CR-570. ✅ `DependenciesPanel` — использует `Tr()` (хорошо)

**Где:** `DependenciesPanel.cpp:17, 22, 25`

**В отличие от большинства диалогов** — здесь локализация работает. **Эталон.**

---

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

---

### CR-580. 🟡 `DependenciesPanel` — `panelTop = 340` может перекрыть ViewCube

**Где:** `DependenciesPanel.cpp:13`

ViewCube справа-сверху. Панель зависимостей — `y=340`. Если ViewCube высотой 300+ — перекрытие. **Проверить визуально.**

**Оценка:** 10 минут.

---

---

## 🔴 P0 — ui/panels (Волна 13, часть 5: RightPanel)

---

