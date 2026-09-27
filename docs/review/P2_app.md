# BreezeCAD — Review: P2 / app

**Записей:** 4

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-127. `OnNewDocument` — тонкая обёртка, но дублирует логику `OnOpen` и `AddDocFromFile`
**Включает (merged):** CR-128, CR-129

**Где:** `Application.cpp:244-248, 452-468`
```
// OnNewDocument
CreateNewDoc(); RefreshView(); FitAll();

// OnOpen
AddDocFromFile(...); AddRecentFile(...); myVisibility.clear(); RefreshView(); FitAll();
```

Паттерн «создать → refresh → fit» повторяется. **Фикс:** `ActivateDocument()`.

---

---

### CR-131. `OnExportSTEP` / `OnExportSTL` — фильтры в диалоге дублируют расширения

**Где:** `Application.cpp:384-386, 421-422`

```
"STEP (*.step)\0*.step\0STEP (*.stp)\0*.stp\0All (*.*)\0*.*\0\0"
```

Формат Win32 — ок. Но дублирование — если меняем формат, надо править в двух местах.

---

---

### CR-163. `OnSketchClick` — точка выхода через `std::cerr` в catch

Уже часть CR-133. Но дополнительно: `catch(...)` без классификации.

---

---

### CR-164. `Application::OnSketchClick` — параметры `int px, int py` — а можно `ImVec2`

Мелочь.

---

---

