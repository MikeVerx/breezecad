# BreezeCAD — Review: P2 / misc

**Записей:** 9

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-027. `SketchTab.h` — неконсистентные дефолты
**Включает (merged):** CR-070, CR-071, CR-073

```
bool         inSketch = false;
LineStyle*   currentLineStyle = nullptr;   // без = nullptr — не единообразно
bool*        hasSketchStart   = nullptr;
```
На самом деле всё с `= nullptr`, но проверь глазами.

---

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

---

### CR-074. `SketchTab.cpp` — `snprintf` без явного включения `<cstdio>`

Уже в CR-022.

---

---

### CR-075. `SketchTab.cpp` — `ImGui::SameLine()` без аргументов внутри `Ограничения`

**Где:** `SketchTab.cpp:135-153`
Паттерн: 6 × `Button + SameLine`, потом снова. **Implicit wrap** — если кнопка не влезает, перескока не будет. **Фикс:** явные `SameLine` и новая строка.

---

---

### CR-076. `AnnotateTab.cpp` — `ICON_DIM_*` — 5 разных иконок, но по канону они **одной группы**

**Где:** `AnnotateTab.cpp:18-28`
Все 5 — large, tooltip отсутствует. OK, но по UI_LAYOUT они могли быть в группе «Размеры» с `GroupStart/End` — то же, что CR-011.

---

---

### CR-077. `ToolsTab.cpp` — `SingleButton` с `"section"` иконкой — **это и есть кнопка «Сечение»**

**Где:** `ToolsTab.cpp:42`
`SingleButton(icons, "section", "tool.section", ICON_SECTION, ...)` — а мы её только что «возвращаем в план». Проверить: где ей место — в Tools или в View?

**По моему плану (Q-11):** в View, группа «Сечения». Значит, **эту надо перенести**, или у Tools оставить как «сечение в анализе», а во View — как основной инструмент.

---

---

### CR-132. `OnExportSTEP` / `OnExportSTL` — нет toast/уведомления об успехе

RULES п.26 / §18.3 UI_LAYOUT — toast-уведомления. Сейчас только `std::cout`.

---

---

### CR-162. `IsPointOnExistingVertex` — `auto dist2 = [](const gp_Pnt& a, const gp_Pnt& b)` — не `constexpr`, не `inline`

Мелочь.

---

---

### CR-185. `DrawSketchOverlay` — пустая функция (B-15, KEEP)

Оставить, но добавить `// TODO (Фаза 2): overlay-подсказки эскиза`.

---

