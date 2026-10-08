# BreezeCAD — Review: P0 / misc

**Записей:** 13

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-049. `ViewTab.cpp` — три одинаковых пункта в выпадашке «Панорамирование»

**Где:** `ViewTab.cpp:21`
**Статус:** OPEN
**Категория:** функциональный баг

**Проблема:**
```
static const char* panOpts[] = { "tool.pan", "tool.pan", "tool.pan" };
```

Меню открывается — пользователь видит «Панорамирование / Панорамирование / Панорамирование».

**Фикс:** оставить один пункт `{ "tool.pan" }` или заменить на реальные варианты (панорама / суперштурвал отключён / …).

**Оценка:** 5 минут.

---

---

### CR-050. `ViewTab.cpp` — `optCount=1` при массиве из 3

**Где:** `ViewTab.cpp:22`

**Проблема:**
```
Button(icons, "nav_pan", panOpts, 1, dummy, ICON_PAN, true, true, ...);
```
`optCount=1` → `hasArrow = (1 > 1) = false` → **стрелка ▼ не появляется**, хотя массив из 3.

Связано с CR-049. Оба фикса — заодно.

---

---

### CR-053. `ViewTab.cpp` — `win_switch` использует ключ от `win_new`

**Где:** `ViewTab.cpp:97-100`

**Проблема:**
```
SingleButton(icons, "win_switch", "tool.new_window", ...);
SingleButton(icons, "win_new",    "tool.new_window", ...);
```

Обе кнопки показывают tooltip «Новое окно». Должно быть `tool.switch_window` для первой.

**Фикс:** проверить `Localization.cpp`, добавить/исправить ключ.

---

---

### CR-118. `OnExportSTL` — нет прогресс-бара

Связано с CR-117. RULES п.26: «Прогресс-бары для всего, что >1 сек».

**Оценка:** входит в CR-117.

---

---

### CR-151. `IsPointOnExistingVertex` — дублирование с `SnapToKnownPoint`

Обе функции **ищут существующие точки эскиза** — Origin + концы линий. Разница:
- `IsPointOnExistingVertex` — bool (есть/нет), с `skip`.
- `SnapToKnownPoint` — возвращает **ближайшую**.

Можно объединить: `FindNearestKnownPoint(p, sketch, skip, threshold, &out)`. Одна — O(N) с `skip`, вторая — O(N) без.

**Фикс:** одна функция.

**Оценка:** 30 минут.

---

---

### CR-731. 🔴 `Localization` — словарь `unordered_map<string, pair<string, string>>` — 3 строки на запись

**Где:** `Localization.cpp:8`

```
const std::unordered_map<std::string, std::pair<std::string, std::string>> kDict = { ... };
```

**Что это значит:**
- Ключ — `std::string` (heap или SSO).
- Значение — `std::pair<std::string, std::string>` (2 heap или SSO).
- **~200 записей × 3 строки = 600 потенциальных аллокаций при инициализации.**
- **`unordered_map` lookup** — hash строки на каждый `Tr()` — **ещё аллокация?** Нет, но хеш-считается каждый раз.

**Плюс:** при инициализации static — все 200+ записей в heap. **Одна большая работа при старте**, но 200 × 2 строки = ~10-20 КБ данных.

**Фикс:**
- `std::map<std::string_view, std::pair<std::string_view, std::string_view>>` — **ноль heap-аллокаций** (всё в string literals).
- Или `constexpr std::array<Entry, N>` + линейный поиск (200 элементов) — cache-friendly.

**Оценка:** 1 час (с рефакторингом Tr).

---

---

### CR-732. 🔴 `Tr()` — если ключа нет, возвращает **ключ целиком**

**Где:** `Localization.h:16`

```
// Перевод по ключу. Если ключа нет — возвращает ключ как есть.
std::string Tr(const std::string& key) const;
```

**Что это значит:** пользователь видит `"panel.deps.fully_constrained"` вместо текста.

**Связано с TECH-DEBT-004** (`tool.project.loop` — не локализован). Это **общий механизм утечки ключей**.

**Фикс:**
- В debug — assert.
- В release — `"[" + key + "]"` с явной маркировкой.

**Оценка:** 15 минут.

---

---

### CR-733. 🔴 `Tr()` не умеет **форматированные** строки

**Где:** `Localization.cpp:139-143`

```
{ "debug.selected.tag",    { "Selected: tag=%d",       "Выбрано: тег=%d" } },
{ "debug.selected.none",   { "Selected: none",         "Выбрано: ничего" } },
```

**Ключ `debug.selected.tag`** содержит `%d`. Но `Tr()` возвращает `std::string` — **где подставляется %d?**

В коде, вероятно: `Text(Tr("debug.selected.tag").c_str(), tag)` — но это **опасно**: если пользователь случайно заложит в перевод лишний `%`, получим UB в `printf`-стиле.

**Фикс:** явное API:
```
std::string TrFmt(const std::string& key, ...);
```
Или через `std::format` (C++20).

**Оценка:** 2 часа.

---

---

### CR-734. 🟠 `Localization.cpp` — ключи `tool.text.line`, `tool.chamfer.2d` и т.д. имеют **лишние пробелы** перед значениями

**Где:** `Localization.cpp:124-140`

```
{ "tool.text.line",                        { "Text on Line",                      "Текст по линии" } },
```

**Множественные пробелы для визуального выравнивания** — при правках легко сбить, diff покажет шум.

**Фикс:** clang-format для словаря (одна конфигурация).

**Оценка:** 10 минут.

---

---

### CR-746. 🟠 `Localization.h` — `Toggle()` метод — не используется?

**Где:** `Localization.h:13`

```
void Toggle() { myLang = (myLang == Language::EN) ? Language::RU : Language::EN; }
```

**Проверить, где вызывается.** Возможно — мёртвый код.

**Оценка:** 2 минуты (проверить).

---

---

### CR-748. 🟠 `Localization.cpp` — строки **переводов** содержат `?` иногда

**Где:** не вижу в первых 150 строках. **Возможно, во второй половине.**

**Проверить.**

---

---

### CR-749. 🟠 `Localization.h` — `enum class Language { EN, RU }` — всего 2 языка

**Где:** `Localization.h:7`

**По `CANON_FEATURES §1`:** «Языковые пакеты расширим после MVP».

**Ок** для MVP.

**WONTFIX.**

---

---

### CR-750. 🟡 `Localization` — `Tr` — имя функции с большой буквы, но **не класс и не тип**

**Где:** `Localization.h:21`

```
inline std::string Tr(const std::string& key) { ... }
```

**По `RULES §10`:** «Функции: camelCase». `Tr` — исключение, но распространено в проекте (Qt-style). **Ок.**

**WONTFIX.**

---

---

