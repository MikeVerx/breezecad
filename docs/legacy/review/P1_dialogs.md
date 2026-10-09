# BreezeCAD — Review: P1 / dialogs

**Записей:** 12

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-143. `OnSketchClick` — не проверяет `myDependencyState` на консистентность

Связано с CR-155.

---

---

### CR-487. Все диалоги — `BreezeDocument*` — прямая зависимость от doc

**Где:** `RenameDialog.h:17`, `DeleteConfirmDialog.h:25`

UI **напрямую вызывает** `doc->RenameFeature(...)` и `doc->RemoveFeature(...)`. Слой `app/` **пропущен**.

**Фикс:** колбэки `onRename(label, name)`, `onDelete(label)` — Application решает.

**Связано с CR-486.**

**Оценка:** входит в CR-486.

---

---

### CR-488. `RenameDialog` / `DeleteConfirmDialog` — нет `nullptr` guard на `ctx.doc`

**Где:** `RenameDialog.cpp:22`, `DeleteConfirmDialog.cpp:24`

```
if (ctx.doc && !ctx.state->pendingLabel.IsNull() && ...) {
```

**Проверка есть — ок.** WONTFIX.

---

---

### CR-489. `UnsavedChangesDialog` — `pendingCloseDocIndex` default `-1`, но `-1` валидный индекс?

**Где:** `UnsavedChangesDialog.h:8`

`-1` как sentinel — общепринято, но **не сентинел для `std::optional`**. Если кто-то передаст `-1` вместо `0`, диалог **не сработает**.

**Фикс:** `std::optional<int>`.

**Оценка:** 15 минут.

---

---

## 🟠 P1 — ui/dialogs (Волна 12, часть 4: AddBoxDialog)

---

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

---

### CR-491. `AddBoxState` — `char name[128]` — риск обрезки UTF-8

**Где:** `AddBoxDialog.h:12`
**Статус:** OPEN

То же, что CR-467 (RenameDialog). Если пользователь введёт длинное имя (с кириллицей) — ImGui обрежет буфер по 127-байтной границе, **посередине символа**.

**Фикс:** `std::string name;` + `InputText` с `std::string` (ImGui 1.90+).

**Оценка:** 15 минут (пакетно с CR-467).

---

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

---

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

---

