# BreezeCAD — Review: P1 / status

**Записей:** 5

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-427. `StatusBar` — `info.sketchActive` управляет показом, но не текущим инструментом

**Где:** `StatusBar.cpp:29`

Показывает «Эскиз» / «Режим ожидания». **А текущий инструмент** (Отрезок / Прямоугольник)? Не показывается.

По UI_LAYOUT §2.1: «Режим ожидания» слева. Возможно, добавить имя инструмента — Фаза 2.

**Отложить.**

---

---

### CR-428. `StatusBar` — `\n` в тексте не используется, но нет `TextUnformatted`

**WONTFIX.**

---

---

### CR-429. `ProjectTabs` — `unsaved` проверяет только пустой path, а не `IsModified`

**Где:** `ProjectTabs.cpp:78`

```
bool unsaved = (ctx.paths && i < (int)ctx.paths->size() && (*ctx.paths)[i].empty());
```

**Проблема:** если проект **сохранён** (path не пустой), но потом **изменён** — `unsaved = false`. Пользователь закрывает без предупреждения.

**Фикс:** `ctx.modified[i]` из `BreezeDocument::IsModified`.

**Оценка:** 20 минут.

---

---

### CR-430. `ProjectTabs` — при закрытии последнего документа ничего не блокируется

**Где:** `ProjectTabs.cpp:120-124`

Кнопка закрытия рисуется только `if (docCount > 1)`. Ок, но если кто-то вызовет `onClose(0)` при `docCount == 1` — `Application::CloseDoc` уже защищён (CR-... в Application). **WONTFIX.**

---

---

### CR-431. `ProjectTabs` — `pendingCloseIdx` и `switchToIdx` — deferred actions после End

**Где:** `ProjectTabs.cpp:138-145`

```
if (pendingCloseIdx >= 0) {
    if (ctx.onClose) ctx.onClose(pendingCloseIdx);
    pendingCloseIdx = -1;
}
if (switchToIdx != -2) {
    if (switchToIdx >= 0 && ctx.onSwitch) ctx.onSwitch(switchToIdx);
    switchToIdx = -2;
}
```

**Правильная** практика — deferred после `ImGui::End()`, иначе при `onClose` меняется `ctx.documentCount` внутри цикла. **Но**:

**Проблема с `-2`:** это magic для «нет действия». А `-1` — тоже «нет» в `pendingCloseIdx`. **Два разных сентинеля.**

**Фикс:** явный `std::optional<int>` или единый `kNoPending = -1`.

**Оценка:** 10 минут.

---

---

## 🔴 P0 — ui/home (Волна 11, часть 1: HomeScreen.h)

---

