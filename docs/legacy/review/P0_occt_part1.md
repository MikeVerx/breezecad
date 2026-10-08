# BreezeCAD — Review: P0 / occt

**Записей:** 47
**Часть:** 1/2

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-088. Пустой `catch (...)` глотает все ошибки

**Где:** `Application.cpp:161`
```
try {
    if (!myOcctView->Init(...)) { myOcctView.reset(); return; }
} catch (...) { myOcctView.reset(); return; }
```

**Молчаливый `return`** + `catch(...)` без логирования. При любой ошибке OCCT приложение тихо не запускается. Пользователь видит пустой чёрный экран.

**Фикс:** как минимум `std::cerr << "OcctView::Init failed\n";`. Как максимум — `std::exception_ptr` + rethrow.

---

---

### CR-100. `UpdateTelemetry`: `myLastShapeCount` — тип?

**Где:** `Application.cpp:1089`
`myLastShapeCount = myOcctView->GetShapeCount();`
`GetShapeCount()` возвращает `int` или `size_t`? Если второе — обрезка. Проверить.

---

---

### CR-148. `EnterSketch` — `planeShape.ShapeType() != TopAbs_FACE` без fallback

**Где:** `Application.cpp:807-810`

```
TopoDS_Shape planeShape = myDocument->GetShape(parentPlane);
if (planeShape.IsNull() || planeShape.ShapeType() != TopAbs_FACE) {
    std::cerr << "EnterSketch: parent is not a face\n";
    return;
}
```

Если плоскость хранится как `COMPOUND` (частая практика в OCCT) — отказ. Пользователь жмёт «Начать эскиз» — ничего не происходит, ни сообщения, ни toast.

**Фикс:** fallback через `TopExp_Explorer(planeShape, TopAbs_FACE)` — взять первую грань.

**Оценка:** 15 минут.

---

---

### CR-251. `Init` — три вызова OCCT-функций продублированы подряд

**Где:** `OcctView.cpp:56-63`

```
myViewer->SetDefaultLights();
myViewer->SetLightOn();
myContext = new AIS_InteractiveContext(myViewer);
myContext->SetDisplayMode(AIS_Shaded, true);

// Двустороннее освещение — плоскости видны с обеих сторон
myViewer->SetDefaultLights();        // дубль
myViewer->SetLightOn();
myContext->SetDisplayMode(AIS_Shaded, true);
```

**Фикс:** удалить второй блок.

**Оценка:** 1 минута.

---

---

### CR-252. Дубли `#include` в `OcctView.cpp`
**Включает (merged):** CR-021, CR-022, CR-245, CR-295, CR-620

- `<AIS_Line.hxx>` — строки 5 и 9
- `<AIS_SelectionModesConcurrency.hxx>` — 16 и 17
- `<Geom_CartesianPoint.hxx>` — 7 и 13
- `<gp_Lin.hxx>` — 24 и 25

**Фикс:** убрать дубли.

**Оценка:** 2 минуты.

---

---

### CR-254. `DisplayShape` — двойная настройка одной shape

**Где:** `OcctView.cpp:157-232`

Сначала через локальный `Prs3d_Drawer`, потом через `myContext->SetColor/SetTransparency/SetDisplayMode`. Комментарий «самый надёжный способ» — признание проблемы.

**Проблема:** два transparency (0.55 и 0.30) конфликтуют. `new Prs3d_Drawer()` на каждую фичу — heap.

**Фикс:** оставить **один** способ через `Prs3d_Drawer`.

**Оценка:** 1 час.

---

---

### CR-255. `DisplayShape` — magic selection mode 4

**Где:** `OcctView.cpp:225-231`

```
myContext->Activate(ais, 4);
myContext->SetSelectionModeActive(ais, 4, true, ...);
```

**Фикс:** `constexpr int kSelectionModeFace = 4;`.

**Оценка:** 5 минут.

---

---

### CR-256. `DisplayShape` — дубликат `label` в `myShapeEntries`

**Где:** `OcctView.cpp:234`

Без проверки на дубликат. При двойном `RefreshView` без `ClearAllShapes` — обновление пойдёт в первый найденный.

**Фикс:** проверка «уже есть» или гарантия `ClearAllShapes`.

**Оценка:** 10 минут.

---

---

### CR-257. `ClearAllShapes` — не сбрасывает выделение

**Где:** `OcctView.cpp:236-245`

`myContext->Remove()` не сбрасывает selected. **Фикс:** `myContext->ClearSelected(false);`.

**Оценка:** 5 минут.

---

---

### CR-258. `BuildSketchGrid` — 84 AIS_Shape на каждый вход в эскиз

**Где:** `OcctView.cpp:262-340`

41 + 41 линии + 2 оси = 84 AIS_Shape, ~400+ heap-аллокаций на вход.

**Фикс:**
- Одна `TopoDS_Compound` — 1 AIS_Shape.
- Или переиспользовать между входами.
- Или виртуальная сетка через ImGui.

**Оценка:** 1-2 часа.

---

---

### CR-259. `BuildSketchGrid` — 3× копипаста блока «линия + аспект + Display»

**Где:** `OcctView.cpp:274-330`

Одинаковый блок 80+ раз.

**Фикс:** `AddGridLine(s, e, color, type, width)`.

**Оценка:** 30 минут.

---

---

### CR-260. `BuildSketchGrid` — magic `ext = 200.0`

**Где:** `OcctView.cpp:265`

Сетка 400×400 мм фиксирована.

**Фикс:** адаптивно от размера детали.

**Оценка:** 20 минут.

---

---

### CR-261. `EnterSketchMode` — magic `500.0` distance и `3.0` scale

**Где:** `OcctView.cpp:354-355`

```
cam->SetDistance(500.0);
cam->SetScale(3.0);
```

**Фикс:** `constexpr` с комментарием.

**Оценка:** 5 минут.

---

---

### CR-262. `EnterSketchMode` — `cam->SetDirection(-normal)` требует визуальной проверки

**Где:** `OcctView.cpp:342`

Смотрим «против» нормали. Возможно правильно (видеть сквозь плоскость), но проверить визуально.

**Оценка:** 10 минут.

---

---

### CR-263. `EnterSketchMode` — `myContext->Deactivate()` глобально

**Где:** `OcctView.cpp:366`

Отключает всю подсветку. Восстанавливается ли в `ExitSketchMode` — проверить в части 2.

---

---

### CR-264. `ExitSketchMode` — код обрезан, нужен полностью

**Где:** `OcctView.cpp:378+`

Скинуть строки 378-450 для полного аудита.

---

---

### CR-265. `OcctView.h` — несогласованный стиль inline-методов

Часть — в одну строку, часть — на нескольких. Дело вкуса. **WONTFIX.**

---

---

### CR-266. `OcctView.h` — C-cast в `GetShapeCount`

**Где:** `OcctView.h:77`

```
int GetShapeCount() const { return (int)myShapeEntries.size(); }
```

**Фикс:** `static_cast<int>(...)`.

**Оценка:** 1 минута.

---

---

### CR-267. `OcctView.h` — `IsSketchMode()` дублирует `IsInSketchMode()`

**Где:** `OcctView.h:76` и `OcctView.h:98`

```
bool IsSketchMode() const { return !myInSketchMode ? false : true; }   // стр. 76
...
bool IsInSketchMode() const { return myInSketchMode; }                  // стр. 98
```

Одинаковое поведение. **Фикс:** удалить `IsSketchMode()`.

**Оценка:** 1 минута.

---

---

### CR-269. `OcctView` — нет метода `Reset()` для полного сброса

При смене документа надо сбросить: shapes, grid, preview, sketch mode, selection. Сейчас 5 отдельных вызовов. Легко забыть один.

**Фикс:** `OcctView::Reset()` — вызывает всё внутри.

**Оценка:** 20 минут.

---

---

### CR-270. `OcctView.h` — 11 полей состояния мыши, часть имён неясна

**Где:** `OcctView.h:107-116`

`myPressX/Y` vs `myLeftPressX/Y` vs `myLastX/Y` — неясная семантика. `myLeftPressed` vs `myLeftDragging` — тоже.

**Фикс:** переименовать или сгруппировать в `struct MouseState { ... }`.

**Оценка:** 15 минут.

---

---

### CR-281. `ExitSketchMode` — восстанавливает shape'ы через `myContext->Display`, но порядок не тот

**Где:** `OcctView.cpp:398-419`

Все shape'ы из `myShapeEntries` переотображаются в **порядке добавления**. Но при выходе из эскиза важна **z-order** (порядок отрисовки): оси → плоскости → деталь. Сейчас — как получится.

**Фикс:** сортировать `myShapeEntries` по типу (системные ниже, фичи выше) или по `IsSystem`.

**Оценка:** 20 минут.

---

---

### CR-282. `ExitSketchMode` — дублирует логику `DisplayShape`

**Где:** `OcctView.cpp:398-419` vs `OcctView.cpp:157-232`

Логика: **то же, что `DisplayShape`**, но:
- Цвета другие (`0.55,0.55,0.55` vs `#c28a23`).
- Transparency другая (`0.95f` vs `0.30f`).
- Проверки другие.

**Это вторая реализация** того же, что `DisplayShape`. Возможные расхождения:
- В `DisplayShape` transparency 0.30 — плоскость 70% видна.
- В `ExitSketchMode` transparency 0.95 — плоскость 5% видна.

**Пользователь выходит из эскиза → плоскости почти исчезают.**

**Это подтверждённый баг.** **Фикс:** переиспользовать `DisplayShape`-логику.

**Оценка:** 40 минут.

---

---

### CR-283. `ScreenToPlane` — magic `1e-9` для denom
**Включает (merged):** CR-285, CR-642, CR-692

**Где:** `OcctView.cpp:436`

```
if (std::abs(denom) < 1e-9) return false;
```

**Фикс:** `constexpr double kMinDenom = 1e-9;`.

**Оценка:** 3 минуты.

---

---

### CR-284. `ScreenToPlane` — `myView.IsNull()` проверяется, но не `myInSketchMode`

**Где:** `OcctView.cpp:426`

```
if (myView.IsNull() || !myInSketchMode) return false;
```

Ок, проверяется. WONTFIX.

---

---

### CR-286. `ShowPreviewLine` — создаёт 3 AIS-объекта на каждый кадр движения мыши

**Где:** `OcctView.cpp:453-503`

**Что происходит на каждый вызов:**
1. `ClearPreviewLine()` — `Remove` × 3 (line, startMarker, endMarker).
2. `new Geom_CartesianPoint` × 2.
3. `new AIS_Line`.
4. `new Prs3d_LineAspect`.
5. `new AIS_Point` × 2.
6. `new Prs3d_PointAspect` × 2.
7. `new Prs3d_Drawer` × 2.
8. `myContext->Display` × 3.
9. `myContext->UpdateCurrentViewer()` + `myView->Redraw()`.

**Итого:** ~10 heap-аллокаций + ~8 GL-обновлений **на каждый кадр движения мыши в эскизе**.

**Цифры:**
- 60 FPS × 10 = **600 аллокаций/сек**.
- Если во время перетаскивания — **1200+/сек**.
- **Плюс OCCT пересчитывает B-rep для точки** — очень дорого.

**Это один из главных RAM/A2-источников.**

**Фикс:**
1. Создавать объекты **один раз** в `EnterSketchMode`, потом **мутировать** их координаты через `SetComponent`.
2. Или — рисовать preview **через ImGui drawList** поверх OCCT-сцены.
3. Или — рисовать preview только при изменении точки (>3px, что уже есть в `Run()`), **не на каждый кадр**.

**Оценка:** 1.5-2 часа.

---

---

### CR-287. `ShowPreviewLine` — `try/catch(...)` глотает всё

**Где:** `OcctView.cpp:499-503`

```
} catch (...) {
    myPreviewLine.Nullify();
    myStartMarker.Nullify();
    myEndMarker.Nullify();
}
```

OCCT-исключения (Standard_Failure) проглатываются. Диагностика невозможна.

**Фикс:** `catch (const std::exception& e) { std::cerr << ...; ... }`.

**Оценка:** 5 минут.

---

---

### CR-288. `ShowCursorMarkerHighlighted` — полный дубликат `ShowCursorMarker`

**Где:** `OcctView.cpp:506-542` vs `OcctView.cpp:544-578`

Единственная разница:
- `ShowCursorMarkerHighlighted(p, true)` → зелёный, size 5.0.
- `ShowCursorMarkerHighlighted(p, false)` → жёлтый, size 3.0.
- `ShowCursorMarker(p)` → жёлтый, size 3.0.

То есть `ShowCursorMarker(p)` **эквивалентен** `ShowCursorMarkerHighlighted(p, false)`.

**Фикс:** удалить `ShowCursorMarker`, оставить `ShowCursorMarkerHighlighted` с дефолтным `highlight = false`.

**Оценка:** 10 минут.

---

---

### CR-289. `ShowCursorMarker` / `ShowCursorMarkerHighlighted` — то же hot-path (см. CR-286)

**Где:** оба метода.

`ShowCursorMarker` вызывается из `Application::Run()` **при каждом движении мыши в эскизе**. Создаёт/удаляет 1 AIS_Point + 1 Prs3d_Drawer + 1 Prs3d_PointAspect = **3 аллокации на кадр**, 180/сек.

**Фикс:** как CR-286 — мутировать существующий объект.

**Оценка:** входит в CR-286.

---

---

### CR-290. `ClearPreviewLine` — вызывается перед **созданием** в `ShowPreviewLine`

**Где:** `OcctView.cpp:461` (в `ShowPreviewLine`) + `OcctView.cpp:570-572` (в `ShowCursorMarker`)

```
// ShowPreviewLine
ClearPreviewLine();   // ← удаляем всё
myPreviewLine = new AIS_Line(...);   // ← создаём заново

// ShowCursorMarker
if (!myStartMarker.IsNull()) { myContext->Remove(...); myStartMarker.Nullify(); }   // ← вручную
if (!myEndMarker.IsNull())   { myContext->Remove(...); myEndMarker.Nullify(); }
if (!myPreviewLine.IsNull()) { myContext->Remove(...); myPreviewLine.Nullify(); }
// ← вручную, а не через ClearPreviewLine
```

**`ShowCursorMarker` дублирует логику `ClearPreviewLine`** вместо вызова.

**Фикс:** `ClearPreviewLine()`.

**Оценка:** 3 минуты.

---

---

### CR-291. `DisplaySketchLine` — width 12.0 для normal — очень толстая

**Где:** `OcctView.cpp:637`

```
default: // Normal
    ...
    width = 12.0;
```

**12.0 px** — очень толстая линия. В ribbon сцене толщина обычно 1-3 px. Возможно, это "чтобы видно было на слабом экране", но **на HiDPI** будет в 12 px = 24 реальных пикселя. **Уточнить.**

**Оценка:** 5 минут.

---

---

### CR-293. `DisplaySketchLine` — `myShapeEntries.push_back` без проверки дубликата

**Где:** `OcctView.cpp:649`

При **второй** раз попадании линии в сцену (например, `RefreshSketchLines` вызвался дважды) — **дубль в `myShapeEntries`**. Потом при `ClearAllShapes` — `Remove` дважды, второй — no-op или crash.

**Фикс:** проверка `label` на дубликат (как CR-256).

**Оценка:** 10 минут.

---

---

### CR-296. `ShowPreviewLine` / `DisplaySketchLine` — `Graphic3d_ZLayerId_Topmost` без проверки

**Где:** `OcctView.cpp:470, 646`

```
myContext->SetZLayer(myPreviewLine, Graphic3d_ZLayerId_Topmost);
```

`Graphic3d_ZLayerId_Topmost` — предопределённый слой. Работает, но:
- Перерисовка Topmost-слоя — **дороже**, чем обычного.
- Если много объектов в Topmost — может замедлить всё.

**Фикс:** проверить, действительно ли нужен Topmost. Для preview — да, но для **постоянных** линий эскиза — нет.

**Оценка:** 10 минут.

---

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

---

### CR-299. `OcctView` — `ShowCursorMarker` в `Application::Run` вызывается **до** `ShowPreviewLine`?

**Где:** проверить в `Application::Run:1200+`

Логика в `Application::Run` (не видел полный код): при движении мыши вызывается `ShowCursorMarker(p)`, при наличии старта — `ShowPreviewLine(start, p)`.

**Порядок важен:** если `ShowPreviewLine` вызывается после `ShowCursorMarker` — он удалит маркер (через `ClearPreviewLine`). Проверить.

---

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

---

### CR-304. `GetSelectedLabel` — использует `==` для `Handle<AIS_InteractiveObject>`

**Где:** `OcctView.cpp:674`

`e.Shape == obj` — сравнение указателей handle. Работает (OCCT перегружает `==`). Ок.

**WONTFIX.**

---

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

---

