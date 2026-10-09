# P0 — КРИТИЧНО — часть 5 из 5

**Записей в части:** 24

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-297. `ShowPreviewLine` / `ShowCursorMarker` — двойной `Redraw`

**Где:** `OcctView.cpp:497-498, 537-538, 573-574`

```
myContext->UpdateCurrentViewer();
if (!myView.IsNull()) myView->Redraw();
```

`UpdateCurrentViewer` уже помечает viewer как "нужна перерисовка". `Redraw()` — принудительно. В **ImmediateUpdate(false)** режиме — это может быть избыточно.

**Фикс:** оставить что-то одно (обычно достаточно `UpdateCurrentViewer()` + последующий кадр ImGui вызовет `Redraw`).

**Оценка:** 10 минут + проверка визуально.

---


### CR-298. `ShowPreviewLine` — `Handle(Geom_Point) gStart = new Geom_CartesianPoint(from)` — уже есть `gp1`

**Где:** `OcctView.cpp:476`

```
Handle(Geom_CartesianPoint) gp1 = new Geom_CartesianPoint(from);   // строка 458
...
Handle(Geom_Point) gStart = new Geom_CartesianPoint(from);         // строка 476, дубль
```

**Двойное создание** одной и той же точки. Плюс `Handle(Geom_Point)` вместо `Handle(Geom_CartesianPoint)` — разный тип handle, но это ок (upcast).

**Фикс:** использовать `gp1`.

**Оценка:** 2 минуты.

---


### CR-299. `OcctView` — `ShowCursorMarker` в `Application::Run` вызывается **до** `ShowPreviewLine`?

**Где:** проверить в `Application::Run:1200+`

Логика в `Application::Run` (не видел полный код): при движении мыши вызывается `ShowCursorMarker(p)`, при наличии старта — `ShowPreviewLine(start, p)`.

**Порядок важен:** если `ShowPreviewLine` вызывается после `ShowCursorMarker` — он удалит маркер (через `ClearPreviewLine`). Проверить.

---


### CR-300. `DisplaySketchLine` — curve может быть trim'нута, `first`/`last` — параметры

**Где:** `OcctView.cpp:604`

```
TopoDS_Edge edge = TopoDS::Edge(line);
Standard_Real first, last;
Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
```

Стандартный паттерн. Ок. Но `Standard_Real` — deprecated (TECH-DEBT-005).

**Фикс:** `double`.

**Оценка:** 2 минуты.

---


### CR-301. `ExitSketchMode` — `myContext->Deactivate()` из `EnterSketchMode` не отменяется

**Где:** `OcctView.cpp:365` (в `EnterSketchMode`)

```
myContext->Deactivate();   // глобально отключает подсветку
```

В `ExitSketchMode` — **нет** соответствующего "ре-активирования". Только `Activate` на отдельных shape'ах. **Глобальный Deactivate** остаётся? Или `Activate` его перекрывает?

**Проверить:** войти в эскиз, выйти, навести на деталь — подсветка работает?

**Оценка:** 10 минут теста.

---

---



### CR-302. `CaptureToPixmap(int width, int height, ...)` игнорирует параметры

**Где:** `OcctView.cpp:687`

```
bool OcctView::CaptureToPixmap(int /*width*/, int /*height*/, const std::string& outPath) {
    ...
    if (!myView->Dump(outPath.c_str())) { ... }
```

Параметры `width`/`height` **закомментированы**. Функция обещает размер, но делает дамп **текущего вьюпорта**. Если Home Screen хочет миниатюру 256×256 — получит что попало.

**Дополнительно:** OCCT `Dump()` без FreeImage поддерживает **только BMP**. Но в `UI::Home::CaptureCurrentThumbnail` сохраняется `.bmp` (см. логи `Thumbnail saved: .bmp`). А в UI_LAYOUT §2.1 миниатюры — PNG. **Формат расходится с каноном.**

**Фикс:**
- Либо реализовать через `V3d_View::Dump` с параметром размера (если API позволит).
- Либо явно отрендерить во временный FBO нужного размера.

**Оценка:** 1 час.

---


### CR-303. `GetSelectedLabel` — O(N) линейный поиск

**Где:** `OcctView.cpp:671-679`

```
for (const auto& e : myShapeEntries) {
    if (e.Shape == obj) return e.Label;
}
```

Вызывается из `OnSketchClick` (в эскизе), из ПКМ-меню, из панелей. При 1000 фич — 1000 сравнений каждый раз.

**Фикс:** `std::unordered_map<Handle(AIS_InteractiveObject), TDF_Label>` — O(1).

**Оценка:** 30 минут.

---


### CR-304. `GetSelectedLabel` — использует `==` для `Handle<AIS_InteractiveObject>`

**Где:** `OcctView.cpp:674`

`e.Shape == obj` — сравнение указателей handle. Работает (OCCT перегружает `==`). Ок.

**WONTFIX.**

---


### CR-305. `OnMouseButton` / `OnMouseMove` / `OnMouseScroll` — магические числа

**Где:**
- `OcctView.cpp:746`: `if (xMax - xMin > 5 && yMax - yMin > 5)` — 5 px
- `OcctView.cpp:807`: `if (dx*dx + dy*dy > 25.0)` — 5 px²
- `OcctView.cpp:749, 761`: `myContext->Select(...)` — параметры без имён

**Фикс:** `constexpr double kMinSelectionRectSize = 5.0;`, `constexpr double kDragStartToleranceSq = 25.0;`.

**Оценка:** 10 минут.

---


### CR-306. `OnMouseButton` — ПКМ в эскизе заблокирован, СКМ — нет

**Где:** `OcctView.cpp:757` vs `OcctView.cpp:768`

```
if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    if (myInSketchMode) return;    // ← блокируем ПКМ
    ...
}
...
} else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
    // ← не блокируем СКМ
```

**Что это значит:**
- В эскизе ПКМ — не вращает камеру (правильно, чтобы не мешать).
- Но СКМ — **панорамирует** (это может быть полезно, но непоследовательно).

**Проверить UX:** хочется ли панорамирование в 2D-эскизе? Если да — оставить. Если нет — заблокировать.

**Оценка:** 5 минут (решение) + 5 минут (код).

---


### CR-307. `OnMouseButton` — `myContext->MoveTo` и `Select` без проверки `myContext.IsNull()`

**Где:** `OcctView.cpp:738-747`

```
if (myLeftPressed && !myLeftDragging) {
    myContext->ClearSelected(false);      // ← нет проверки
    myContext->MoveTo(px, py, myView, false);
    myContext->SelectDetected(AIS_SelectionScheme_Replace);
    ...
}
```

`myContext` может быть `IsNull()` (если `Init` не был вызван). Падение.

**Хотя:** `OcctView::OnMouseButton` вызывается через GLFW-callback только после `Init`. Формально ок. Но защита от регрессий — нужна.

**Фикс:** ранний `if (myContext.IsNull()) return;` в начале.

**Оценка:** 2 минуты.

---


### CR-308. `OnMouseMove` — `myView->Pan()` без проверки `myView.IsNull()`

**Где:** `OcctView.cpp:817-821`

```
if (myPanning) {
    int lpx, lpy;
    CursorToFb(myGlfwWindow, myLastX, myLastY, lpx, lpy);
    myView->Pan(px - lpx, -(py - lpy));   // ← нет проверки
    myView->Redraw();
}
```

Аналогично CR-307. **Плюс:** `myView->Pan` тоже без проверки на null.

**Фикс:** ранний guard.

**Оценка:** 2 минуты.

---


### CR-309. `SetDisplayModeAll` использует `DefaultDrawer` — глобальное влияние

**Где:** `OcctView.cpp:713-720`

```
void OcctView::SetDisplayModeAll(int mode, bool edges) {
    if (myContext.IsNull()) return;
    myContext->SetDisplayMode(mode, false);
    Handle(Prs3d_Drawer) dr = myContext->DefaultDrawer();   // ← глобальный drawer
    dr->SetFaceBoundaryDraw(edges);
    myContext->UpdateCurrentViewer();
    ...
}
```

`DefaultDrawer()` — **общий drawer для всех объектов**, у которых нет своего. Меняем `SetFaceBoundaryDraw(edges)` — влияет **глобально**, включая системные объекты (оси/плоскости), у которых должен быть свой aspect.

**Фикс:** либо явный проход по `myShapeEntries` и настройка каждого, либо явное документирование «это глобальная настройка».

**Оценка:** 30 минут.

---


### CR-310. `SetCameraDirectionAndUp` — магические 0.99 / 0.9

**Где:** `OcctView.cpp:752-757`

```
if (std::abs(dir.Dot(up)) > 0.99) {
    gp_Dir fallback(0, 0, 1);
    if (std::abs(dir.Dot(fallback)) > 0.9) fallback = gp_Dir(0, 1, 0);
    safeUp = fallback;
}
```

`0.99` и `0.9` — magic. Логика: «если dir и up почти параллельны, взять другой up». Но зачем **два разных** порога (0.99 и 0.9)?

**Фикс:** единая константа `kMinPerpendicularDot = 0.99` (или обосновать разные).

**Оценка:** 5 минут.

---


### CR-311. `RotateAroundUp` — `M_PI / 180.0`

**Где:** `OcctView.cpp:786`

```
rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), up), angleDeg * M_PI / 180.0);
```

**Фикс:** `constexpr double kDegToRad = M_PI / 180.0;` (в `util/Geometry2D` уже должно быть).

**Оценка:** 5 минут.

---


### CR-312. `CursorToFb` — статическая функция, дублирует логику из `Application`

**Где:** `OcctView.cpp:698-706`

```
static void CursorToFb(GLFWwindow* w, double x, double y, int& outX, int& outY) {
    int winW = 1, winH = 1, fbW = 1, fbH = 1;
    glfwGetWindowSize(w, &winW, &winH);
    glfwGetFramebufferSize(w, &fbW, &fbH);
    double sx = (winW > 0) ? (double)fbW / winW : 1.0;
    double sy = (winH > 0) ? (double)fbH / winH : 1.0;
    outX = (int)(x * sx);
    outY = (int)(y * sy);
}
```

**То же самое**, что `Application::Run` делает inline (CR-061). Вынести в `platform/glfw/GlfwWindow` helper.

**Оценка:** 20 минут.

---


### CR-313. `SetHomeView` — хардкод направления `(-1, -1, -1)` без объяснения

**Где:** `OcctView.cpp:790-793`

```
cam->SetDirection(gp_Dir(-1, -1, -1));
cam->SetUp(gp_Dir(0, 0, 1));
```

Стандартная изометрия. Но в коде — не задокументировано.

**Фикс:** `constexpr gp_Dir kIsoDirection{-1, -1, -1};` + комментарий.

**Оценка:** 3 минуты.

---


### CR-314. `FitAll` — magic `0.01` в `FitAll` и `ZFitAll`

**Где:** `OcctView.cpp:722-727`

```
void OcctView::FitAll() {
    myView->FitAll(0.01, false);   // ← 0.01 = margin
    myView->ZFitAll();
    myView->Redraw();
}
```

`0.01` — margin (1%). Повторяется в `SetHomeView` (строка 799) и `ExitSketchMode` (строка 421).

**Фикс:** `constexpr double kFitAllMargin = 0.01;`.

**Оценка:** 5 минут.

---


### CR-315. `OnMouseScroll` — magic `40` в `yoffset * 40`

**Где:** `OcctView.cpp:832`

```
myView->ZoomAtPoint(px, py, px, py + (int)(yoffset * 40));
```

`40` — px на «шаг» колеса. На разных ОС/драйверах — разный. У пользователя трекпад может дать `yoffset = 0.1` → `4 px`.

**Фикс:** брать из настроек / использовать `io.MouseWheel` от ImGui.

**Оценка:** 10 минут.

---


### CR-316. `Resize` — не обновляет `myWidth`/`myHeight` перед проверкой

**Где:** `OcctView.cpp:681-687`

```
void OcctView::Resize(int width, int height) {
    myWidth  = width;
    myHeight = height;
    if (myWindow.IsNull() || myView.IsNull()) return;
    myWindow->SetSize(width, height);
    myView->MustBeResized();
}
```

Порядок: сначала обновляет поля, потом guard. Если `myWindow.IsNull()`, поля всё равно обновлены. Формально ок, но порядок не идиоматичен.

**WONTFIX** — работает.

---


### CR-317. `SelectAtPixel` и `OnMouseButton` — двойной код выделения

**Где:** `OcctView.cpp:661-668` vs `OcctView.cpp:739-746`

```
// SelectAtPixel
myContext->ClearSelected(false);
myContext->MoveTo(px, py, myView, false);
myContext->SelectDetected(AIS_SelectionScheme_Replace);
myContext->UpdateCurrentViewer();
myView->Redraw();

// OnMouseButton — то же самое
myContext->ClearSelected(false);
myContext->MoveTo(px, py, myView, false);
myContext->SelectDetected(AIS_SelectionScheme_Replace);
myContext->UpdateCurrentViewer();
myView->Redraw();
```

**Полная копипаста.** Фикс: `OnMouseButton` → вызов `SelectAtPixel(px, py)`.

**Оценка:** 5 минут.

---


### CR-318. `OnMouseButton` — двойной `myView->Redraw()`

**Где:** `OcctView.cpp:746, 754`

В ветке RELEASE с выделением — `Redraw()` в конце. И `myView->Redraw()` в ветке `myLeftDragging`. Ок, но проверять.

**WONTFIX.**

---


### CR-319. `Redraw()` — тонкая обёртка без проверки `IsNull` в самом деле

**Где:** `OcctView.cpp:801`

```
void OcctView::Redraw() {
    if (!myView.IsNull()) myView->Redraw();
}
```

Ок.

---


### CR-320. `OcctView.cpp` — `M_PI` без явного `<cmath>` гарантии

**Где:** `OcctView.cpp:14` — есть `#include <cmath>`, но `M_PI` — не стандарт C++ (POSIX extension). На MSVC требует `_USE_MATH_DEFINES`. На MinGW работает. **Для кроссплатформенности** (macOS, ARM) — использовать `std::numbers::pi` (C++20) или свой `constexpr double kPi = 3.14159265358979323846;`.

**Оценка:** 5 минут.

---

