# P1 — СЕРЬЁЗНО — часть 2 из 2

**Записей в части:** 20

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-212. `RequestDelete` — `snprintf` не проверяет переполнение

**Где:** `Application.cpp:1023`

Если имя фичи очень длинное (>200 симв.), `snprintf` обрежет. Может обрезать посередине UTF-8 → кракозябры.

**Фикс:** проверка длины имени перед snprintf.

---


### CR-213. `CMakeLists.txt` — нет явной переменной версии OCCT

**Где:** весь файл
Нужно знать, какую версию OCCT собираем против. Для диагностики кэша (`.bzc`).

**Фикс:** `message(STATUS "OCCT version: ${OpenCASCADE_VERSION}")`.

---


### CR-214. `CMakeLists.txt` — нет установки target для `install()`

**Где:** весь файл
Нет `install(TARGETS breezecad)`. Нельзя сделать `make install` / `ninja install`. Для portable-сборки — пока не критично, но для CI пригодится.

---


### CR-215. `CMakeLists.txt` — порядок source-файлов не группирован

**Где:** `CMakeLists.txt:55-108`
Файлы идут вперемешку. **Фикс:** группировка комментариями (`# app`, `# ui/ribbon`, `# doc`).

---


### CR-216. `CMakeLists.txt` — ImGui — `add_library(imgui STATIC ...)` без target_compile_options

ImGui под GCC нужен `-Wno-...` (иначе предупреждения). Сейчас их нет, но если включим `-Wall` — ImGui будет спамить.

**Фикс:** `target_compile_options(imgui PRIVATE -w)` (отключить warnings для чужой либы).

---



### CR-227. `IconManager::CreateTexture` — нет проверки `w`, `h`

**Где:** `IconManager.cpp:65-75`
Если lunasvg вернёт пустой bitmap (w=0 или h=0) — `glTexImage2D` с нулевыми размерами. Некорректная текстура.

**Фикс:** проверка.

---


### CR-228. `IconManager::RasterizeToTexture` — предупреждение при NOT FOUND на каждый вызов

**Где:** `IconManager.cpp:225-228`

```
if (foundPath.empty()) {
    std::cerr << "IconManager: NOT FOUND " << realName << ".svg (alias for " << name << ")\n";
    myCache[key] = 0;
    return 0;
}
```

**Кэшируется ноль**, но `std::cerr` пишется **один раз** на ключ (потому что потом идёт `myCache.find(key)` → hit). Ок. Но при **рестарте приложения** — снова пишет. **5 NOT FOUND иконок (TECH-DEBT-004) × каждого запуска.**

**Фикс:** убрать или понизить до TRACE.

---


### CR-229. `IconManager::RasterizeToTexture` — `bitmap` не проверяется на `data() != nullptr`

**Где:** `IconManager.cpp:155`

```
const uint8_t* src = bitmap.data();
```

Если bitmap создан, но `data()` = nullptr (редкий случай) — краш в цикле конвертации.

**Фикс:** `if (!src) return 0;`.

---


### CR-230. `IconManager` — `std::stringstream buf` — можно `std::string` + resize

**Где:** `IconManager.cpp:106-109`

```
std::stringstream buf;
buf << file.rdbuf();
std::string svgData = buf.str();
```

`stringstream` делает **двойную копию**. Для 105 иконок × ~2 КБ — не критично, но можно через `std::istreambuf_iterator` или `std::string((istreambuf_iterator<char>(file)), {})`.

**Оценка:** 5 минут.

---


### CR-231. `IconManager::PreloadDefaults` — `static const std::vector<std::string>` создаётся один раз

Но если функция вызывается на **каждом Application** — инициализация `std::vector<std::string>` **статическая локальная** — thread-safe-init C++11. Ок.

Но `PreloadAll` вызывается **на каждый Application**. Если Application пересоздаётся — грузит **заново** (в новый GL-контекст), но старые текстуры — утечка (см. RAM-H1). **Связано.**

---



### CR-271. `OcctView.cpp` — инициализация `myGridVisible = true`, но сетка не построена

**Где:** `OcctView.h:143` + `OcctView.cpp`

`myGridVisible = true` по умолчанию. Но `BuildSketchGrid` вызывается только в `EnterSketchMode`. В 3D-режиме `myGridVisible = true` — а сетки нет. Если кто-то проверит флаг — получит **неверное состояние**.

**Фикс:** `myGridVisible = false` до первого `BuildSketchGrid`.

**Оценка:** 2 минуты.

---


### CR-272. `OcctView::SetShapeVisible` — silent fail при отсутствии label

**Где:** `OcctView.cpp:302-317`

Если `label` не найден в `myShapeEntries` — цикл заканчивается, **ничего не делается, никакой диагностики**. Возможны расхождения между `myVisibility` в `Application` и реальным состоянием сцены.

**Фикс:** debug-warning при ненайденной label.

**Оценка:** 5 минут.

---


### CR-273. `OcctView::Init` — порядок `glfwMakeContextCurrent` до `new Aspect_DisplayConnection`

**Где:** `OcctView.cpp:40-44`

```
glfwMakeContextCurrent(myGlfwWindow);   // ← сначала
myDisplayConnection = new Aspect_DisplayConnection();
```

Работает. Но `Aspect_DisplayConnection` на Windows не требует GL — он про окно. Ок.

**WONTFIX.**

---


### CR-274. `OcctView::Init` — `myWindow->SetVirtual(true)`

**Где:** `OcctView.cpp:104`

`SetVirtual(true)` — обычно для offscreen rendering. Для real-window — `false`. Проверить, не мешает ли.

**Оценка:** 10 минут.

---


### CR-275. `OcctView::Init` — `myWindow->IsMapped()` и `Map()` без проверки состояния

**Где:** `OcctView.cpp:134-135`

```
if (!myWindow->IsMapped()) myWindow->Map();
```

Идиоматично. ОК.

**WONTFIX.**

---


### CR-276. `OcctView` — `SetImmediateUpdate(false)` — что за ним

**Где:** `OcctView.cpp:99`

```
myView->SetImmediateUpdate(false);
```

Отключает немедленный redraw при изменениях. Нужно явно вызывать `Redraw()`. Это **правильно**, но проверь, что **все** точки изменения вызывают `Redraw()`.

**Оценка:** audit в части 2.

---


### CR-277. `OcctView.cpp` — `#include <AIS_Line.hxx>` в `.h` и `.cpp`

**Где:** `OcctView.h:5`, `OcctView.cpp:9`

`AIS_Line` используется в `myPreviewLine` (member). Точно нужен в `.h`. В `.cpp` — если уже приходит через header, лишний.

**Оценка:** 2 минуты.

---


### CR-278. `OcctView::DisplayShape` — `isPlane2` проверяется вторично

**Где:** `OcctView.cpp:213-221`

```
if (isSystem) {
    bool isPlane2 = (shape.ShapeType() == TopAbs_FACE);   // ← снова то же
```

В блоке выше (`isSystem`) уже было `isPlane = (shape.ShapeType() == TopAbs_FACE)`. Дублирование.

**Фикс:** переиспользовать `isPlane`.

**Оценка:** 5 минут.

---


### CR-279. `OcctView::DisplayShape` — магические цвета `0.761, 0.541, 0.137` vs `IM_COL32`

**Где:** `OcctView.cpp:168-170`

```
Quantity_Color faceFill(0.761, 0.541, 0.137, Quantity_TOC_RGB);  // #c28a23
Quantity_Color faceEdge(0.588, 0.357, 0.125, Quantity_TOC_RGB);  // #965b20
```

RULES п.30: цвета **от темы**. Здесь хардкод, дублируется дважды (в двух блоках).

**Фикс:** `myTheme.tokens.planeFaceColor` и т.д.

**Оценка:** 20 минут.

---


### CR-280. `OcctView.cpp` — `std::cout` в `EnterSketchMode` / `ExitSketchMode`

**Где:** `OcctView.cpp:369` и другие

Связано с CR-133. **Фикс:** TRACE.

---

---


