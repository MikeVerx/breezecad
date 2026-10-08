# BreezeCAD — Review: P0 / util

**Записей:** 21

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-311. `RotateAroundUp` — `M_PI / 180.0`
**Включает (merged):** CR-320, CR-643, CR-667, CR-691

**Где:** `OcctView.cpp:786`

```
rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), up), angleDeg * M_PI / 180.0);
```

**Фикс:** `constexpr double kDegToRad = M_PI / 180.0;` (в `util/Geometry2D` уже должно быть).

**Оценка:** 5 минут.

---

---

### CR-679. 🔴 `Utf8.cpp` — жёстко зашит `wchar_t = 16 бит` — КРОССПЛАТФОРМЕННЫЙ БАГ

**Где:** `Utf8.cpp:37, 47` (Utf8ToExt), `:64` (ExtToUtf8)
**Статус:** OPEN
**Категория:** кроссплатформенность — блокирует Mac/Linux/ARM

**Utf8ToExt:**
```
ws.push_back((wchar_t)cp);
...
ws.push_back((wchar_t)(0xD800 + (cp >> 10)));   // ← сурогатная пара
ws.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
```

Код **сознательно** кодирует 4-байтные UTF-8 символы как **сурогатные пары UTF-16**. Это работает на **Windows** (где `wchar_t = 16 бит`).

**Но:**
- **Linux:** `wchar_t = 32 бита` → `(wchar_t)0xD800` — это **валидный code point U+D800** (зарезервированный), а не сурогат. **Строка сломается.**
- **macOS (Intel/Apple Silicon):** то же — 32 бита.
- **ARM везде:** зависит от ОС, обычно 32.

**ExtToUtf8:**
```
const uint16_t* ws = (const uint16_t*)es.ToExtString();
...
if (cp >= 0xD800 && cp <= 0xDBFF && ws[i+1] != 0) {
    uint32_t lo = ws[i+1];
    if (lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
```

**Жёсткий каст к `uint16_t*`**. На Linux — 32-битные code units → **читаем половину каждого символа**. Полный мусор.

**Что это значит для проекта:**
- **Проект НЕ соберётся/НЕ заработает на macOS** при первом запуске (кириллические имена фич).
- **Android/ARM** — то же.
- **Целевая кроссплатформенность** (Philosophy, `CANON_FEATURES`, `ADR-0001`) — **блокируется этим файлом**.

**Фикс:**

**Вариант A:** использовать OCCT API:
```
TCollection_ExtendedString Utf8ToExt(const std::string& s) {
    return TCollection_ExtendedString(s.c_str(), Standard_True);  // isMultiByte=true
}
std::string ExtToUtf8(const TCollection_ExtendedString& es) {
    TCollection_AsciiString ascii(es, '?');  // потеря не-ASCII → но CR-705 фиксит
    return ascii.ToCString();
}
```

**Вариант B:** использовать `Standard_Utf8` API (OCCT 7.7+):
- `Standard_Utf8Char` тип.
- `TCollection_ExtendedString` уже умеет работать с UTF-8.

**Вариант C:** собственная реализация с `#ifdef WCHAR_IS_16BIT`:
```
#if WCHAR_MAX == 0xFFFF
    // Windows — сурогатные пары
#else
    // POSIX — прямой код-поинт
#endif
```

**Рекомендую A** — меньше кода, OCCT всё умеет.

**Оценка:** 2 часа + кроссплатформенное тестирование.

---

---

### CR-680. 🔴 `Utf8.cpp` — `ws.push_back(0)` — добавление null-терминатора вручную

**Где:** `Utf8.cpp:41`

```
ws.push_back(0);
return TCollection_ExtendedString(ws.c_str());
```

`std::wstring::c_str()` **уже** null-терминирован. Дополнительный `push_back(0)` — **двойной null** в конце. OCCT игнорирует, но это лишнее.

**Плюс:** `ws.reserve(s.size())` — **недооценка**. UTF-8 `s.size()` — число байт. UTF-16 `ws.size()` = для кириллицы равно, для эмодзи вдвое меньше, но для ASCII равно. **Фактически** reserve может быть недостаточно для эмодзи (в 2× больше push_back).

**Фикс:** убрать `push_back(0)`.

**Оценка:** 2 минуты.

---

---

### CR-682. 🔴 `Utf8ToExt` — невалидный UTF-8 → молчаливый пропуск

**Где:** `Utf8.cpp:24-26`

```
else { i++; continue; }   // ← невалидный байт — пропускаем
```

**Проблема:** если на входе **битый UTF-8** — молча теряем байт. Диагностика невозможна.

**Фикс:** `assert` в debug, лог в release.

**Оценка:** 10 минут.

---

---

### CR-683. 🔴 `Utf8ToExt` — `if (i + extra >= s.size()) break;` — обрезка хвоста

**Где:** `Utf8.cpp:22`

```
if (i + extra >= s.size()) break;
```

**Обрезка** при неполной последовательности — ок, но **без предупреждения**.

**Оценка:** входит в CR-682.

---

---

### CR-684. 🟠 `Utf8.cpp` — `(wchar_t)cp` — C-cast + potential truncation

**Где:** `Utf8.cpp:37`

```
ws.push_back((wchar_t)cp);
```

Если `cp = 0x10000..0x10FFFF` (4-байтный UTF-8), а `wchar_t = 16 бит` (Windows) — **усечение** до 16 бит. Затем проверка `if (cp < 0x10000)` — уже поздно.

**Смотрю ещё раз код:**
```
if (cp < 0x10000) {
    ws.push_back((wchar_t)cp);      // ← для cp < 0x10000
} else if (cp <= 0x10FFFF) {
    cp -= 0x10000;                   // ← для 4-байтных
    ws.push_back((wchar_t)(0xD800 + (cp >> 10)));
    ws.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
}
```

**Ок**, усечения нет — логика корректна.

**WONTFIX.**

---

---

### CR-685. 🟠 `Utf8.cpp` — `wcstombs` / `mbrtowc` — почему не использовать стандартные?

**WONTFIX** — OCCT не совместим со стандартными, но идея — использовать OCCT-API (CR-679).

---

## 🔴 P0 — util (Волна 15, часть 2: Geometry2D)

---

### CR-687. 🔴 `SnapToGridPoint` — то же: `origin` используется

**Где:** `Geometry2D.cpp:64, 78-80`

```
gp_Pnt origin = ax.Location();
...
gp_Pnt snapped = origin;   // ← используется
```

**Ок.** WONTFIX.

**Но:** в `ApplyOrtho`:

**Где:** `Geometry2D.cpp:36`

```
gp_Pnt origin = ax.Location();   // ← объявлена
...
gp_Pnt result = from;            // ← origin НЕ используется
```

**То же, что CR-686.** Использовать `from` вместо `origin`.

**Оценка:** 1 минута.

---

---

### CR-689. 🔴 `Geometry2D.cpp` — 4× дублирование `ax.Location()/XDirection()/YDirection()`

**Где:** `Geometry2D.cpp:22-24, 36-38, 66-68, 86-88`

В **каждой** из 4 функций:
```
gp_Ax3 ax = plane.Position();
gp_Pnt origin = ax.Location();
gp_Dir xDir   = ax.XDirection();
gp_Dir yDir   = ax.YDirection();
```

**4 копии.** Связано с CR-019 (RULES п.19 — одна функция одно место).

**Фикс:** `struct PlaneAxes { gp_Pnt origin; gp_Dir x, y; }` + `GetPlaneAxes(plane)`.

**Оценка:** 30 минут.

---

---

### CR-690. 🔴 `Geometry2D.cpp` — `gp_Vec(xDir)` — конструктор gp_Vec на каждой операции

**Где:** `Geometry2D.cpp:29-30, 46-47, 75-76, 95-96`

```
double u = v.Dot(gp_Vec(xDir));   // ← конструктор
double w = v.Dot(gp_Vec(yDir));   // ← конструктор
```

`gp_Vec(gp_Dir)` — не бесплатный, копирование координат. 4 функции × 2-3 вызова = **~10 конструкторов** на каждый вызов `Geometry2D`.

Для `SnapToGridPoint`, вызываемого **на каждый пиксель движения мыши в эскизе** — это 600+ конструкторов/сек.

**Фикс:** `gp_Vec xVec(xDir);` один раз, переиспользовать.

Или — **лучше** — `gp_Dir::Dot(gp_Vec)` уже умеет: `xDir.Dot(gp_Vec(from, to))` без конверсии? Проверить API.

**Оценка:** 20 минут.

---

---

### CR-693. 🟠 `Geometry2D` — `ApplyOrtho`: `u2 = (u >= 0) ? len : -len;` — теряет знак если len = 0

**Где:** `Geometry2D.cpp:48-54`

`len < 1e-6` уже проверено. Ок.

**WONTFIX.**

---

---

### CR-694. 🟠 `Geometry2D` — `ComputeLengthAngle` не нормализует `angleDeg` в [0, 360)

**Где:** `Geometry2D.cpp:31`

```
angleDeg = std::atan2(w, u) * 180.0 / M_PI;
```

`std::atan2` возвращает [-π, π] → `angleDeg ∈ [-180, 180]`. Отрицательные углы — ок для математики, но пользователю показывать «-45°» не всегда удобно.

**WONTFIX** — если UI сам нормализует.

---

---

### CR-695. 🟠 `Geometry2D` — `gp_Pnt result = from; result.Translate(...)` — мутация

**Где:** `Geometry2D.cpp:56-58, 78-80, 97-99`

```
gp_Pnt result = from;   // copy
result.Translate(gp_Vec(xDir) * u2);   // mutate
result.Translate(gp_Vec(yDir) * w2);   // mutate
return result;
```

**Работает**, но `gp_Pnt::Translate` — мутирующий метод. Правильнее:
```
return from.Translated(gp_Vec(xDir) * u2 + gp_Vec(yDir) * w2);
```
Меньше копий, чище.

**Оценка:** 10 минут.

---

---

### CR-696. 🟠 `Geometry2D.h` — комментарий «без побочных эффектов» — но функции возвращают `gp_Pnt`

**Где:** `Geometry2D.h:6`

```
// Все функции — без состояния, без побочных эффектов.
```

Формально верно (нет static, нет глобалов). Но `ComputeLengthAngle` использует out-параметры `double& length, double& angleDeg` — это **побочный эффект** через ссылки.

**Фикс:** возвращать `struct { double length; double angleDeg; }` или `std::pair`.

**Оценка:** 15 минут.

---

## 🔴 P0 — util (Волна 15, часть 3: LabelName)

---

### CR-698. 🔴 `LabelKey` — `ostringstream` для одной строки

**Где:** `LabelName.cpp:14-17`

```
std::ostringstream oss;
label.EntryDump(oss);
return oss.str();
```

**`ostringstream`** — тяжёлый (heap, locale, virtual call на `operator<<`). Для **каждого узла дерева × каждый кадр × каждый lookup в map** = **тысячи `ostringstream` в секунду**.

**Связано с CR-093, CR-610** — тоже про `LabelKey`.

**Фикс:**
- `TDF_Label::EntryDump` имеет перегрузку в `char*`? Проверить.
- Или использовать `label.Entry()` — возвращает `TDF_Entry` (v. 7.x) или `uint64_t` — hashable.
- Или: `std::to_string(label.Tag())` + `father` рекурсивно.

**Рекомендую:** `uint64_t LabelKey(const TDF_Label&)` — комбинация `Tag()` и hash `Entry()`. O(1), без heap, hashable напрямую.

**Оценка:** 1 час (переписать + обновить всех потребителей: DocumentTree, Visibility, ContextMenu, Application).

---

---

### CR-700. 🟠 `LabelName.cpp` — `#include <sstream>` только для одного использования

**Где:** `LabelName.cpp:6`

`sstream` — тяжёлый (5 КБ кода). Используется в одной функции `LabelKey`, которая **перепишется** (CR-698).

**Фикс:** после CR-698 убрать `#include <sstream>`.

**Оценка:** входит в CR-698.

---

---

### CR-701. 🟠 `LabelName.h` — комментарий «См. BUG в TECH_DEBT» — но BUG-A5 в REFACTOR_NOTES

**Где:** `LabelName.h:8-9`

```
// Известное ограничение: GetLabelName использует AsciiString,
// поэтому кириллические имена теряются. См. BUG в TECH_DEBT.
```

**Комментарий ссылается на `TECH_DEBT`**, а BUG-A5 — в **`REFACTOR_NOTES §A`**. Мелочь, но путает.

**Фикс:** актуализировать или убрать после фикса CR-697.

**Оценка:** 2 минуты.

---

---

### CR-702. 🟠 `LabelName.h` — `LabelKey` возвращает `std::string`, но нужен `uint64_t`

**Где:** `LabelName.h:18`

**Связано с CR-698.** После фикса — поменять API.

**Оценка:** входит в CR-698.

---

---

### CR-703. 🟠 `Utf8.h` — функции возвращают объекты по значению (heap)

**Где:** `Utf8.h:16-17`

```
TCollection_ExtendedString Utf8ToExt(const std::string& s);
std::string ExtToUtf8(const TCollection_ExtendedString& es);
```

Каждый вызов = **heap-аллокация**:
- `TCollection_ExtendedString` — внутри OCCT handle (refcount + heap).
- `std::string` — heap.

**Для `ExtToUtf8` вызываемого при каждом рендере дерева** (через `LabelName`, CR-697) — **60 FPS × 500 узлов = 30000 аллокаций/сек.**

**Связано с CR-093, CR-610.**

**Фикс:**
- `void ExtToUtf8(const TCollection_ExtendedString& es, std::string& out);` — переиспользуемый буфер.
- Или `const std::string&` кэш.

**Оценка:** 1 час.

---

---

### CR-704. 🟠 `Geometry2D` — 4 out-параметра в `ComputeLengthAngle`

**Где:** `Geometry2D.h:22-26`

```
void ComputeLengthAngle(const gp_Pln& plane,
                        const gp_Pnt& from,
                        const gp_Pnt& to,
                        double& length,
                        double& angleDeg);
```

**2 out-параметра** — антипаттерн. Связано с CR-696.

**Фикс:** `struct LengthAngle { double length; double angle; };` — return by value.

**Оценка:** 20 минут.

---

---

### CR-705. 🟠 `Utf8.h` — `#include <TCollection_ExtendedString.hxx>` в публичном заголовке

**Где:** `Utf8.h:13`

`util/Utf8.h` **подключает** OCCT-заголовок. Значит **любой файл**, включающий `Utf8.h`, тянет OCCT.

**Что это значит:** `util/` не является независимым слоем. Связано с `ARCHITECTURE §2` — `util/` должен быть **чистым C++**, без OCCT.

**Фикс:** либо **убрать** `Utf8.h` из публичного API (только `.cpp`), либо — использовать **непрозрачный** интерфейс:
```
std::string ExtToUtf8Opaque(const void* ext);
void* Utf8ToExtOpaque(const std::string&);
```
Или — **отказаться** от OCCT в `util/Utf8`, сделать pure-C++ API. Тогда `Application`/`BreezeDocument` будут конвертировать сами.

**Оценка:** 2 часа (архитектурно).

---

## 📊 Итог util (Волна 15)

**Закрыто модулей util:** 3 из 3 (Utf8, Geometry2D, LabelName)
**Найдено записей:** 27 (CR-679..CR-705)

**Критические:**
- **CR-679** — кроссплатформенный баг Utf8 (блокирует Mac/Linux/ARM).
- **CR-697** — BUG-A5 (кириллица теряется в дереве).
- **CR-698** — `LabelKey` через `ostringstream` (hot-path).
- **CR-686, 687** — dead variable `origin`.

---

---

## 🔴 P0 — platform (Волна 16, часть 1: FileDialog)

---

