# BreezeCAD — Review: P0 / viewcube

**Записей:** 30

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-626. 🔴 `ViewCubeGeometry.h` — только TODO, ни одной структуры

**Где:** `ViewCubeGeometry.h:14-20`
**Статус:** OPEN
**Категория:** незаконченный рефакторинг + корень BUG-4.1

```
// TODO(step 1.3):
//   struct Face   { int v[4]; int n[3]; const char* label; };
//   struct Edge   { int v1, v2; int f1, f2; };
//   struct Corner { int v; int f1, f2, f3; };
//   inline constexpr Face   kFaces[6]   = { ... };
//   inline constexpr Edge   kEdges[12]  = { ... };
//   inline constexpr Corner kCorners[8] = { ... };
//   inline constexpr double kFaceTextRight[6][3] = { ... };
//   inline constexpr double kFaceTextDown[6][3]  = { ... };
```

**Файл существует, но ПУСТОЙ.** Вся геометрия куба — где-то в `ViewCubeOverlay.cpp`.

**Почему это корень BUG-4.1:**
- BUG-4.1: «подписи привязаны к неверным граням при вращении».
- Если данные `kFaces/kFaceTextRight/kFaceTextDown` в `.cpp` (не в отдельном хедере) — их нельзя протестировать, нельзя переиспользовать, нельзя аккуратно починить.
- **Без выноса таблиц в `ViewCubeGeometry.cpp` BUG-4.1 не исправить чисто.**

**Фикс:** реализовать то, что в TODO — вынести таблицы. Файл 21 строка должен стать ~150 строк.

**Оценка:** 2 часа (правильно).

---

---

### CR-627. 🔴 `ViewCubeAnimator.cpp` — 3 строки, реализация в другом месте

**Где:** `ViewCubeAnimator.cpp` (3 строки)
**Статус:** OPEN
**Категория:** незаконченный рефакторинг

Файл `.cpp` **существует**, но содержит только `#include` (вероятно). Реализация `Update/StartAbsolute/StartRoll` — в `ViewCubeOverlay.cpp`.

**Связано с CR-626.** Рефакторинг `ui/viewcube` был сделан **наполовину**: файлы созданы, но логика осталась в основном `.cpp`.

**Фикс:** вынести методы `Animator` из `ViewCubeOverlay.cpp` в `ViewCubeAnimator.cpp`.

**Оценка:** 1 час.

---

---

### CR-629. 🟠 `Animator::AnimTarget` — `int kind` вместо `enum class`

**Где:** `ViewCubeAnimator.h:15-19`

```
struct AnimTarget {
    int    kind;          // 0 = absolute, 1 = roll
    double dir[3];
    double up[3];
};
```

`kind` — magic `0/1`. Комментарий говорит что.

**Фикс:** `enum class Kind { Absolute, Roll };`.

**Оценка:** 20 минут.

---

---

### CR-630. 🟠 `AnimTarget` — `double dir[3]`, `up[3]` — сырые массивы

**Где:** `ViewCubeAnimator.h:17-18`

**Что теряется:**
- Никакой валидации (нормализация).
- `dir` и `up` могут быть коллинеарны → gimbal lock в камере.
- Копирование вручную.

**Фикс:** `gp_Dir dir; gp_Dir up;` (OCCT) или свой `Vec3`.

**Связано с CR-596** (gp_Dir в UI).

**Оценка:** 30 минут.

---

---

### CR-634. 🟠 `ViewCubeOverlay::myButtonsTimer` — что это?

**Где:** `ViewCubeOverlay.h:44`

```
float    myButtonsTimer = 0.0f;
```

**Нет комментария.** Что за «buttons timer»? Скрытие кнопок << >> через N секунд? Или что-то другое?

**Фикс:** комментарий или переименование.

**Оценка:** 2 минуты (уточнить).

---

---

### CR-635. 🟠 `ViewCubeOverlay` — `Context::io` — `ImGuiIO&` внутри

**Где:** `ViewCubeOverlay.h:36`

```
ImGuiIO& io;
```

`ImGui::GetIO()` — глобал. Передавать как параметр — **не помогает**, потому что всё равно синглтон. **Иллюзия инъекции.**

**WONTFIX** — так принято в ImGui.

---

---

### CR-636. 🟡 `ViewCubeOverlay() = default;` + `~ViewCubeOverlay() = default;`

**Где:** `ViewCubeOverlay.h:41-42`

Не нужны явно, если не делать ничего особенного. Стиль. **WONTFIX.**

---

---

## 🔴 P0 — ui/viewcube (Волна 14, часть 2: Overlay.cpp 1-260)

---

### CR-638. 🔴 `ViewCubeOverlay.cpp` — таблицы `Face/Edge/Corner` инлайн

**Где:** `ViewCubeOverlay.cpp:127-135, 158-166, 168-175`
**Статус:** OPEN
**Категория:** корень BUG-4.1 (см. CR-626)

```
struct Face { int v[4]; gp_Dir n; const char* label; };
Face faces[6] = {
    {{4,5,6,7}, {0,0, 1}, "ВЕРХ"},
    {{0,3,2,1}, {0,0,-1}, "НИЗ"},
    {{0,1,5,4}, {0,-1,0}, "ПЕРЕД"},
    ...
};

Edge edges[12] = { ... };
Corner corners[8] = { ... };
```

**Всё это в теле `Draw()`**, пересоздаётся каждый кадр:
- 6 `Face` (struct ~40 байт) = 240 байт.
- 12 `Edge` = 48 байт.
- 8 `Corner` = 32 байта.
- **Плюс `gp_Dir` в каждом Face — constructors OCCT вызываются 60×6 = 360 раз/сек.**

**Связано с CR-626** (пустой `ViewCubeGeometry.h`).

**Фикс:** вынести в `ViewCubeGeometry.h` как `inline constexpr`.

**Оценка:** 2 часа (входит в CR-626).

---

---

### CR-640. 🔴 `ViewCubeOverlay::Draw` — 3 × `std::vector` + `std::sort` каждый кадр

**Где:** `ViewCubeOverlay.cpp:138, 177, 187`

```
std::vector<Visible> vis;      // до 3 элементов
std::vector<VisEdge> visEdges; // до 12
std::vector<VisCorner> visCorners; // до 8
...
std::sort(vis.begin(), vis.end(), ...);
```

**Каждый кадр:**
- 3 `std::vector` — **3 heap-аллокации** (или реаллокации при росте).
- `std::sort` — O(N log N) на 3 элементах.
- Возможно, cap растёт.

**Фикс:**
- `std::array<Visible, 6> vis; int visCount = 0;`
- Или `static` буферы (но тогда thread-safety).
- Или small-vector оптимизация.

**Оценка:** 1 час.

---

---

### CR-644. 🔴 `ViewCubeOverlay.cpp` — `myAnimator.myToDir[0]` — прямое чтение public

**Где:** `ViewCubeOverlay.cpp:33-115` — **вся логика анимации**

Прямой доступ к `myAnimator.myActive`, `myAnimator.myT`, `myAnimator.myFromDir[0]`, `myAnimator.myToDir[0]`, `myAnimator.myQueue` — **более 30 обращений** к public-полям.

**Связано с CR-628** («потом закроем» — не закрыли).

**Плюс:** логика анимации **вся в Draw**, а не в `Animator::Update(dt)`.

**Фикс:** вынести в `Animator::Update(dt, view)` — один вызов.

**Оценка:** 2 часа (входит в CR-627).

---

---

### CR-645. 🔴 `ViewCubeOverlay.cpp` — `#include "imgui_internal.h"`

**Где:** `ViewCubeOverlay.cpp:6`

```
#include "imgui_internal.h"
```

ImGui **internal API** — может сломаться между версиями. Где используется? В видимом фрагменте — **не вижу**. Возможно, во второй половине.

**Фикс:** проверить, где нужно; если для `ImDrawList::AddText` — есть в публичном.

**Оценка:** 10 минут.

---

---

### CR-647. 🟠 `ViewCubeOverlay.cpp` — `Face.n.Dot(camDir)` — OCCT-математика в UI

**Где:** `ViewCubeOverlay.cpp:139`

```
if (faces[fi].n.Dot(camDir) >= -0.01) continue;
```

`0.01` — magic tolerance. **Фикс:** `constexpr double kFaceCullTolerance = 0.01;`.

**Оценка:** входит в CR-641.

---

---

### CR-649. 🟠 `ViewCubeOverlay.cpp` — `hoveredCorner/Edge/Face` — 3 int'а

**Где:** `ViewCubeOverlay.cpp:212-214`

Три int-а для hover-состояния. **Логически** — tri-state enum:
- Ничего не наведено.
- Наведена грань.
- Наведено ребро.
- Наведён угол.

**Фикс:** `struct Hovered { enum Kind {None, Face, Edge, Corner}; Kind kind; int idx; }`.

**Оценка:** 20 минут.

---

---

### CR-651. 🟠 `ViewCubeOverlay.cpp` — hit-testing грани через signed area

**Где:** `ViewCubeOverlay.cpp:249-258`

```
for (int e = 0; e < 4; e++) {
    ImVec2 a = v.pts[e], b = v.pts[(e+1)%4];
    float cr = (b.x-a.x)*(mouse.y-a.y) - (b.y-a.y)*(mouse.x-a.x);
    int s = (cr > 0) ? 1 : -1;
    if (sign == 0) sign = s;
    else if (sign != s) { inside = false; break; }
}
```

**Корректная** реализация point-in-polygon. Норм.

**WONTFIX.**

---

---

### CR-652. 🟠 `ViewCubeOverlay.cpp` — `ImVec2 mouse = io.MousePos;` — можно из `io.MousePos` напрямую

Незначительно. **WONTFIX.**

---

---

### CR-653. 🟠 `ViewCubeOverlay.cpp` — 3 `for (int k = (int)visCorners.size()-1; k >= 0; k--)` — C-cast

**Где:** `ViewCubeOverlay.cpp:221, 233, 245`

```
for (int k = (int)visCorners.size() - 1; k >= 0; k--)
```

**C-cast** + обратная итерация. **Фикс:** `static_cast<int>`.

**Оценка:** 2 минуты.

---

---

### CR-654. 🟠 `ViewCubeOverlay.cpp` — `if (!io.WantCaptureMouse)` — hover только если ImGui не ест

**Где:** `ViewCubeOverlay.cpp:216`

**Правильно.** WONTFIX.

---

---

### CR-656. 🟡 `ViewCubeOverlay.cpp` — комментарий «Плавная анимация камеры: 0.5 сек»

**Где:** `ViewCubeOverlay.cpp:36`

Комментарий есть — хорошо. Но `0.5f` (строка 38) — magic. **Входит в CR-641.**

---

---

## 🔴 P0 — ui/viewcube (Волна 14, часть 3: Overlay.cpp 260-524)

---

### CR-658. 🔴 `ViewCubeOverlay.cpp` — рендер текста через `AddImageQuad` — сложная кастомная логика
**Включает (merged):** CR-657, CR-659, CR-660, CR-661, CR-681

**Где:** `ViewCubeOverlay.cpp:288-380`

**Что делает:**
1. Ручной UTF-8 парсинг символов (30 строк).
2. Для каждого глифа — `FindGlyph(cp)`.
3. `scale = fSizePx / font->FontSize`.
4. Вычисление 4 углов квада в 3D-координатах грани.
5. Проекция каждого через `project()`.
6. `AddImageQuad` с UV из атласа.

**Проблемы:**
- **200 строк кастомного кода** вместо ~20 строк стандартного ImGui-рендера.
- Каждый глиф — 4 вызова `project()` = 4 двойных умножения + сложения. Для 5 букв × 3 видимых грани × 60 FPS = **3600 matrix-ops/сек**.
- **Кастомный рендер = кастомные баги** (что и есть BUG-4.1).

**Фикс (варианты):**
- **A.** Использовать `ImDrawList::AddText` с поворотом — но `AddText` не поддерживает 3D-проекцию. **Не подходит.**
- **B.** Рендерить надписи **плоско** на экране (без 3D-проекции) — угол обзора чуть хуже, но **просто и надёжно**. Для ViewCube это **визуально приемлемо** (в Inventor подписи тоже «плавают», но остаются горизонтальными).
- **C.** Оставить 3D-проекцию, но **упростить код** через `ImDrawList::PathArcTo` / helper.

**Рекомендую B** — упрощает в 10 раз, устраняет класс багов, визуально почти не отличается.

**Оценка:** 2 часа (B).

---

---

### CR-662. 🔴 `startVCAnim` + `startVCRoll` — 30 строк копипасты

**Где:** `ViewCubeOverlay.cpp:410-448` (startVCAnim) + `:450-475` (startVCRoll)

**Оба метода:**
1. Проверяют `if (myAnimator.myActive || !myAnimator.myQueue.empty())` — magic.
2. Проверяют `if (myAnimator.myQueue.size() < 8)` — magic.
3. Создают `AnimTarget`, заполняют поля по одному.
4. Записывают `myFromDir/Up[0..2]`, `myToDir/Up[0..2]`.
5. `myT = 0`, `myActive = true`.

**Разница:** только `kind = 0/1` и содержимое полей.

**Фикс:** `Animator::Start(kind, ...)`, `Animator::Enqueue(kind, ...)`.

**Оценка:** 1 час (после CR-628).

---

---

### CR-663. 🔴 `ViewCubeOverlay.cpp` — логика показа кнопок << >> запутана

**Где:** `ViewCubeOverlay.cpp:490-514`
**Статус:** OPEN
**Категория:** логическая сложность

```
if (cubeAligned && hoveredFace >= 0) {
    myButtonsTimer = 0.5f;
} else if (cubeAligned && mouseInBtnZone && myButtonsTimer > 0.0f) {
    myButtonsTimer = 0.5f;
} else {
    myButtonsTimer -= io.DeltaTime;
    if (myButtonsTimer < 0.0f) myButtonsTimer = 0.0f;
}
```

**Что это значит:**
1. Если кубик выровнен и курсор над гранью → кнопки показать на 0.5s.
2. Если кубик выровнен и курсор в btnZone и таймер ещё тикает → продлить.
3. Иначе → таймер тикает вниз.

**Magic `0.5` два раза.** Logic **неясна** без комментария — «показывать кнопки 0.5 сек после hover, если курсор в зоне».

**Плюс:** `mouseInBtnZone` вычисляется через `stickyL/T/R/B` с magic 80/70/100.

**Фикс:** 
- `constexpr float kButtonsShowSeconds = 0.5f;`
- Явный state machine (Show/Hide/Transitioning).

**Оценка:** 1 час.

---

---

### CR-668. 🟠 `ViewCubeOverlay.cpp` — `<cmath>` для `std::sqrt` — но `M_PI` из `<cmath>` не гарантирован

**Где:** `ViewCubeOverlay.cpp:11`

Уже разбирали (CR-320).

---

---

### CR-669. 🟠 `ViewCubeOverlay.cpp` — `vis.size() == 1` — «кубик выровнен»

**Где:** `ViewCubeOverlay.cpp:484`

```
bool cubeAligned = (vis.size() == 1);
```

**Работает** — если видна ровно 1 грань, камера смотрит точно перпендикулярно. **Но:** `vis.size()` — до **сортировки** может быть 1, после — то же. Ок.

**Фикс:** комментарий.

**Оценка:** 2 минуты.

---

---

### CR-672. 🟠 `ViewCubeOverlay.cpp` — кнопки рендерятся через новое ImGui-окно

**Где:** `ViewCubeOverlay.cpp:521`

```
ImGui::Begin("##viewcube_arrows", ...);
```

**Ок**, но: для 2 кнопок создаётся отдельное окно. Можно было бы нарисовать через draw-list напрямую (как весь остальной ViewCube).

**Не критично.** WONTFIX.

---

---

### CR-673. 🟠 `ViewCubeOverlay.cpp` — tooltip не показывается, если кнопка disabled

**WONTFIX.**

---

---

### CR-674. 🟠 `ViewCubeOverlay.cpp` — `rollCameraAnim` lambda — лишний слой

**Где:** `ViewCubeOverlay.cpp:515-517`

```
auto rollCameraAnim = [&](double angleDeg) {
    startVCRoll(angleDeg);
};
```

**Оборачивает `startVCRoll` без логики.** Фикс: вызывать `startVCRoll` напрямую.

**Оценка:** 1 минута.

---

---

### CR-675. 🟠 `ViewCubeOverlay.cpp` — большие `Face`-таблицы копируются каждый кадр

**Где:** `ViewCubeOverlay.cpp:127-135`

**Уже** CR-638. Дополнение: `gp_Dir n` внутри `Face` — конструктор **вызывается 60 FPS × 6 грани × повторно = 360 раз/сек**.

**Оценка:** входит в CR-638.

---

---

### CR-676. 🟡 `ViewCubeOverlay.cpp` — `if (vis.size() == 1)` — `size_t == int`

**Где:** `ViewCubeOverlay.cpp:484`

`vis.size()` возвращает `size_t`, `1` — `int`. Неявное сравнение.

**Фикс:** `vis.size() == 1u` или `(int)vis.size() == 1`.

**Оценка:** 1 минута.

---

---

### CR-677. 🟡 `ViewCubeOverlay.cpp` — `AddImageQuad(atlasTex, ...)` — ищет глиф каждый кадр

**Где:** `ViewCubeOverlay.cpp:371`

`font->FindGlyph((ImWchar)cp)` — вызывается на каждый символ подписи, каждый кадр. Для 3 подписей по 4-5 букв = **15 FindGlyph/кадр × 60 FPS = 900/сек.**

`FindGlyph` делает бинарный поиск по `IndexAdvanceX`/`IndexLookup` — O(log N). Для 3000 глифов в атласе — 11 итераций.

**Фикс:** кэш глифов по codepoint в статике (уже есть внутри ImGui — `FindGlyph` — часть кэша). Не критично.

**WONTFIX.**

---

---

### CR-678. 🟡 `ViewCubeOverlay.cpp` — `theme.cubeText` для всех символов всех граней

**Где:** `ViewCubeOverlay.cpp:373`

**Один цвет** для текста. Может, для hoveredFace — другой? Не реализовано.

**Отложено (Фаза 2).**

---

## 🎯 Сводка по ViewCube (корень BUG-4.1)

**BUG-4.1 состоит из 3 частей:**
1. **CR-657** — таблицы `faceTextRight/Down` захардкожены и содержат ошибки.
2. **CR-658** — кастомный 3D-рендер текста слишком сложен → баги.
3. **CR-659** — ручной UTF-8 парсинг → усечение → «ВЛЕВО» вместо «ЛЕВО».

**План фикса BUG-4.1:**
1. Вынести таблицы в `ViewCubeGeometry.{h,cpp}` (CR-626).
2. Заменить ручной UTF-8 на `ImTextCharFromUtf8` (CR-659).
3. **Рассмотреть замену 3D-рендера на плоский экранный** (CR-658, вариант B) — простое и надёжное.
4. Юнит-тест на геометрию (6 граней × 6 камер).

**Оценка полная:** 4-6 часов.

---

---

## 🔴 P0 — util (Волна 15, часть 1: Utf8)

---

