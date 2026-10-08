# BreezeCAD — Review: P2 / ribbon

**Записей:** 4

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

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

---

### CR-018. `namespace A { namespace B {` вместо `namespace A::B {`

**Где:** все файлы
**Статус:** DEFERRED (до clang-tidy)

C++17 позволяет `namespace BreezeCAD::UI::Ribbon {`. Экономит строки и indent.

**Фикс:** массовая замена через regex + clang-format. Отдельный коммит.

---

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

---

### CR-063. `RibbonButton.cpp` — `min/max` через два отдельных вызова
**Включает (merged):** CR-064, CR-065, CR-066, CR-067, CR-068, CR-069

**Где:** `RibbonButton.cpp:59-60`
```
ImVec2 min = ImGui::GetItemRectMin();
ImVec2 max = ImGui::GetItemRectMax();
```
Норм, но `min`/`max` — плохие имена (конфликт с `std::min/max` в глазах читателя). **Фикс:** `rMin`, `rMax`.

---

---

