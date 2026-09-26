# P0 — КРИТИЧНО — часть 4 из 5

**Записей в части:** 46

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-241. `GetAllShapes` — возвращает `vector<TopoDS_Shape>` по значению

**Где:** `BreezeDocument.cpp:400-406`

```
std::vector<TopoDS_Shape> BreezeDocument::GetAllShapes() const {
    std::vector<TopoDS_Shape> shapes;
    if (myDoc.IsNull()) return shapes;
    CollectShapes(MainLabel(), shapes);   // O(N) элементов
    return shapes;                        // ← копия / move
}
```

Вызывается в `OnExportSTEP`, `OnExportSTL`. Для 1000 фич — 1000 `TopoDS_Shape` копируется (каждый — Handle с инкрементом refcount).

**Фикс:** RVO/NRVO сработает, но не для всех компиляторов. `std::move` явно. Или функция `CollectShapes(vector&)` как API — вызывающий сам создаёт вектор.

**Оценка:** 10 минут.

---


### CR-242. `ClearSketch` — стилевые метки не очищаются

**Где:** `BreezeDocument.cpp:335-350`

```
std::vector<TDF_Label> toRemove;
TDF_ChildIterator it(sketchLabel, false);
for (; it.More(); it.Next()) toRemove.push_back(it.Value());
for (auto& l : toRemove) l.ForgetAllAttributes(true);
```

Обходим **прямых** детей sketch. Но каждый `lineLabel` имеет **style-ребёнка** (tag 100). Он не забывается.

**Связано с CR-236.** **Фикс:** рекурсивный Forget.

---


### CR-243. `RemoveLastSketchLine` — style остаётся

То же, что CR-242. После удаления последней линии — style-метка (tag 100) остаётся в OCAF. Через `AddSketchLine` при совпадении tag 100 — перезапишется (ок), но между удалением и созданием — «висячая» метка.

---


### CR-244. `BreezeDocument` — `std::cout` в 12+ местах

Связано с CR-133. **Фикс:** переход на `TRACE` / убрать.

---


### CR-245. `BreezeDocument.cpp` — неиспользуемые include

**Где:** начало файла

```
#include <TCollection_AsciiString.hxx>   // ← не используется (BUG-A5 — в LabelName)
#include <gp_Lin.hxx>                     // ← не используется
```

**Фикс:** удалить.

---


### CR-246. `AddSketchFeature` — счётчик «Sketch1, Sketch2» через количество

**Где:** `BreezeDocument.cpp:245-249`

```
int count = 0;
TDF_ChildIterator it(parentPlane, false);
for (; it.More(); it.Next()) count++;
std::string name = "Sketch" + std::to_string(count + 1);
```

**Проблема:** если у пользователя были Sketch1, Sketch2, удалил Sketch1 → count = 1 → новый эскиз назовётся «Sketch2» — **дубликат имени**.

**Фикс:** монотонный счётчик `myNextSketchNumber` (per-document) или сканировать имена и брать max+1.

**Оценка:** 15 минут.

---


### CR-247. `BreezeDocument` — магические теги `10`, `100`

**Где:** `BreezeDocument.cpp:229` (myNextFeatureId начинается с 10 в CreateBaseGeometry), `BreezeDocument.cpp:275` (tag 100 для style)

**Проблема:** `100` для style, `10` для первого user feature — нигде не задокументировано, кроме кода. Не понятно почему 100, а не 10.

**Фикс:**
```
constexpr int kFirstUserFeatureTag = 10;
constexpr int kSketchStyleChildTag = 100;
```

**Оценка:** 5 минут.

---


### CR-248. `GetType` — молча возвращает `FT_Other` при отсутствии атрибута

**Где:** `BreezeDocument.cpp:260-266`

```
int BreezeDocument::GetType(const TDF_Label& label) {
    Handle(TDataStd_Integer) attr;
    if (label.FindAttribute(TDataStd_Integer::GetID(), attr)) return attr->Get();
    return FT_Other;
}
```

Если метка должна быть `FT_Box`, но атрибут потерян (баг), вернётся `FT_Other` — фича "тихо" переклассифицируется. Утечка диагностики.

**Фикс:** логировать `WARN` при неожиданном `FT_Other` на непустой метке.

**Оценка:** 5 минут.

---


### CR-249. `IsSystem` — магия `< 0`

**Где:** `BreezeDocument.cpp:268-270`

```
bool BreezeDocument::IsSystem(const TDF_Label& label) {
    return GetType(label) < 0;
}
```

Семантика «system = отрицательный type». Работает, но неявно. Если заведём новый не-system с отрицательным type — сломается.

**Фикс:** явный список (`FT_Start`, `FT_Plane`, `FT_Axis`, `FT_OriginPoint`).

**Оценка:** 5 минут.

---


### CR-250. `CollectShapes` — без защиты от циклов

**Где:** `BreezeDocument.cpp:390-398`

```
void BreezeDocument::CollectShapes(const TDF_Label& label, std::vector<TopoDS_Shape>& out) const {
    ...
    TDF_ChildIterator it(label, false);
    for (; it.More(); it.Next()) CollectShapes(it.Value(), out);   // рекурсия
}
```

OCAF-дерево не должно иметь циклов, но если баг приведёт к таковому — **stack overflow**. Защита: max-depth.

**Оценка:** 5 минут.

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


### CR-252. Дубли `#include` в `OcctView.cpp`

- `<AIS_Line.hxx>` — строки 5 и 9
- `<AIS_SelectionModesConcurrency.hxx>` — 16 и 17
- `<Geom_CartesianPoint.hxx>` — 7 и 13
- `<gp_Lin.hxx>` — 24 и 25

**Фикс:** убрать дубли.

**Оценка:** 2 минуты.

---


### CR-253. `~OcctView() = default` — потенциальная GPU-утечка

**Где:** `OcctView.cpp:35`

Все handle'ы освободятся автоматически. **Но** `myContext` не вызывает `Remove()` для отображаемых объектов. OCCT не гарантирует очистку GL-ресурсов без явного `RemoveAll`.

**Фикс:**
```
OcctView::~OcctView() {
    if (!myContext.IsNull()) {
        myContext->RemoveAll(false);
        myContext->UpdateCurrentViewer();
    }
}
```

**Оценка:** 15 минут.

---


### CR-254. `DisplayShape` — двойная настройка одной shape

**Где:** `OcctView.cpp:157-232`

Сначала через локальный `Prs3d_Drawer`, потом через `myContext->SetColor/SetTransparency/SetDisplayMode`. Комментарий «самый надёжный способ» — признание проблемы.

**Проблема:** два transparency (0.55 и 0.30) конфликтуют. `new Prs3d_Drawer()` на каждую фичу — heap.

**Фикс:** оставить **один** способ через `Prs3d_Drawer`.

**Оценка:** 1 час.

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


### CR-256. `DisplayShape` — дубликат `label` в `myShapeEntries`

**Где:** `OcctView.cpp:234`

Без проверки на дубликат. При двойном `RefreshView` без `ClearAllShapes` — обновление пойдёт в первый найденный.

**Фикс:** проверка «уже есть» или гарантия `ClearAllShapes`.

**Оценка:** 10 минут.

---


### CR-257. `ClearAllShapes` — не сбрасывает выделение

**Где:** `OcctView.cpp:236-245`

`myContext->Remove()` не сбрасывает selected. **Фикс:** `myContext->ClearSelected(false);`.

**Оценка:** 5 минут.

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


### CR-259. `BuildSketchGrid` — 3× копипаста блока «линия + аспект + Display»

**Где:** `OcctView.cpp:274-330`

Одинаковый блок 80+ раз.

**Фикс:** `AddGridLine(s, e, color, type, width)`.

**Оценка:** 30 минут.

---


### CR-260. `BuildSketchGrid` — magic `ext = 200.0`

**Где:** `OcctView.cpp:265`

Сетка 400×400 мм фиксирована.

**Фикс:** адаптивно от размера детали.

**Оценка:** 20 минут.

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


### CR-262. `EnterSketchMode` — `cam->SetDirection(-normal)` требует визуальной проверки

**Где:** `OcctView.cpp:342`

Смотрим «против» нормали. Возможно правильно (видеть сквозь плоскость), но проверить визуально.

**Оценка:** 10 минут.

---


### CR-263. `EnterSketchMode` — `myContext->Deactivate()` глобально

**Где:** `OcctView.cpp:366`

Отключает всю подсветку. Восстанавливается ли в `ExitSketchMode` — проверить в части 2.

---


### CR-264. `ExitSketchMode` — код обрезан, нужен полностью

**Где:** `OcctView.cpp:378+`

Скинуть строки 378-450 для полного аудита.

---


### CR-265. `OcctView.h` — несогласованный стиль inline-методов

Часть — в одну строку, часть — на нескольких. Дело вкуса. **WONTFIX.**

---


### CR-266. `OcctView.h` — C-cast в `GetShapeCount`

**Где:** `OcctView.h:77`

```
int GetShapeCount() const { return (int)myShapeEntries.size(); }
```

**Фикс:** `static_cast<int>(...)`.

**Оценка:** 1 минута.

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


### CR-268. `OcctView.h` — `GetAllLabels()` возвращает `vector` по значению

**Где:** `OcctView.h:80-85`

Каждый `TDF_Label` — refcount inc/dec. Вызывается в панелях для изоляции.

**Фикс:** `const std::vector<...>&` или span/итераторный интерфейс.

**Оценка:** 15 минут.

---


### CR-269. `OcctView` — нет метода `Reset()` для полного сброса

При смене документа надо сбросить: shapes, grid, preview, sketch mode, selection. Сейчас 5 отдельных вызовов. Легко забыть один.

**Фикс:** `OcctView::Reset()` — вызывает всё внутри.

**Оценка:** 20 минут.

---


### CR-270. `OcctView.h` — 11 полей состояния мыши, часть имён неясна

**Где:** `OcctView.h:107-116`

`myPressX/Y` vs `myLeftPressX/Y` vs `myLastX/Y` — неясная семантика. `myLeftPressed` vs `myLeftDragging` — тоже.

**Фикс:** переименовать или сгруппировать в `struct MouseState { ... }`.

**Оценка:** 15 минут.

---



### CR-281. `ExitSketchMode` — восстанавливает shape'ы через `myContext->Display`, но порядок не тот

**Где:** `OcctView.cpp:398-419`

Все shape'ы из `myShapeEntries` переотображаются в **порядке добавления**. Но при выходе из эскиза важна **z-order** (порядок отрисовки): оси → плоскости → деталь. Сейчас — как получится.

**Фикс:** сортировать `myShapeEntries` по типу (системные ниже, фичи выше) или по `IsSystem`.

**Оценка:** 20 минут.

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


### CR-283. `ScreenToPlane` — magic `1e-9` для denom

**Где:** `OcctView.cpp:436`

```
if (std::abs(denom) < 1e-9) return false;
```

**Фикс:** `constexpr double kMinDenom = 1e-9;`.

**Оценка:** 3 минуты.

---


### CR-284. `ScreenToPlane` — `myView.IsNull()` проверяется, но не `myInSketchMode`

**Где:** `OcctView.cpp:426`

```
if (myView.IsNull() || !myInSketchMode) return false;
```

Ок, проверяется. WONTFIX.

---


### CR-285. `ShowPreviewLine` — magic `1e-6` для минимальной длины

**Где:** `OcctView.cpp:451`

```
if (dx*dx + dy*dy + dz*dz < 1e-6) return;
```

**Фикс:** `constexpr double kMinPreviewLenSq = 1e-6;`.

**Оценка:** 3 минуты.

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


### CR-289. `ShowCursorMarker` / `ShowCursorMarkerHighlighted` — то же hot-path (см. CR-286)

**Где:** оба метода.

`ShowCursorMarker` вызывается из `Application::Run()` **при каждом движении мыши в эскизе**. Создаёт/удаляет 1 AIS_Point + 1 Prs3d_Drawer + 1 Prs3d_PointAspect = **3 аллокации на кадр**, 180/сек.

**Фикс:** как CR-286 — мутировать существующий объект.

**Оценка:** входит в CR-286.

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


### CR-292. `DisplaySketchLine` — цвет хардкод (RULES п.30)

**Где:** `OcctView.cpp:626-644`

```
case 1: color = Quantity_Color(0.55, 0.55, 0.75, ...);  // Construction
case 2: color = Quantity_Color(0.9, 0.45, 0.15, ...);   // Centerline
default: color = Quantity_Color(0.75, 0.2, 1.0, ...);   // Normal
```

Плюс эти же цвета **дублируются** в `SketchTab.cpp` (CR-046). Два источника правды → разъедутся.

**Фикс:** единый `ThemeTokens::lineStyleColor(style)`.

**Оценка:** 30 минут.

---


### CR-293. `DisplaySketchLine` — `myShapeEntries.push_back` без проверки дубликата

**Где:** `OcctView.cpp:649`

При **второй** раз попадании линии в сцену (например, `RefreshSketchLines` вызвался дважды) — **дубль в `myShapeEntries`**. Потом при `ClearAllShapes` — `Remove` дважды, второй — no-op или crash.

**Фикс:** проверка `label` на дубликат (как CR-256).

**Оценка:** 10 минут.

---


### CR-294. `DisplaySketchLine` — `false` для isSystem, но линии эскиза — полу-системные

**Где:** `OcctView.cpp:649`

```
myShapeEntries.push_back({ label, ais, line, false });   // isSystem = false
```

`isSystem = false` значит при `RefreshView` линии эскиза будут **скрыты, если есть 3D-операции** (см. `Application::RefreshView:222-229`).

Но по логике — линии эскиза должны быть **видны всегда**, пока активен эскиз.

**Проверить:** на самом деле после выхода из эскиза линии должны исчезнуть из сцены. Но `isSystem` в `myShapeEntries` используется не так — используется в `RefreshView` через `BreezeDocument::IsSystem`. Не путать.

**Возможно WONTFIX**, но проверить.

---


### CR-295. `OcctView.cpp` — `#include <gp_Lin.hxx>` дважды, `<AIS_Line.hxx>` дважды, `<Geom_CartesianPoint.hxx>` дважды (в части 1 было)

См. CR-252. Часть уже зафиксирована. Здесь дополнительно:
- `<Prs3d_LineAspect.hxx>` — не вижу, но используется (`Aspect_TOL_DASH`, `SetLineAspect`).
- `<Aspect_TypeOfLine.hxx>` — есть.
- `<Graphic3d_ZLayerId.hxx>` — используется `Graphic3d_ZLayerId_Topmost`, не вижу include.

**Фикс:** добавить пропущенные.

**Оценка:** 5 минут.

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


