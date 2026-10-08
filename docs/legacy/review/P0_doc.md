# BreezeCAD — Review: P0 / doc

**Записей:** 22

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-122. `AddDocFromFile` / `OnOpen` — нет проверки на дубликат

**Где:** `Application.cpp:269, 452`

Пользователь открывает `foo.cbf` дважды → два `BreezeDocument` с одним файлом. При сохранении один перезапишет другой.

**Фикс:** проверить `myProjectPaths` на наличие `path`, если есть — `SwitchToDoc`.

**Оценка:** 10 минут.

---

---

### CR-142. `OnSketchClick` — дубль создания edge

**Где:** `Application.cpp:757-760`

```
myDocument->AddSketchLine(myActiveSketchLabel, mySketchStartPoint, p, myCurrentLineStyle);
myOcctView->ClearPreviewLine();

TopoDS_Shape line = BRepBuilderAPI_MakeEdge(mySketchStartPoint, p).Shape();  // ← ЗАЧЕМ?
```

`AddSketchLine` уже создал edge и сохранил в OCAF. Здесь создаётся **второй, независимый** edge **только** для `DisplaySketchLine`. OCCT кидает его в heap.

**Фикс:** `BreezeDocument::AddSketchLine` **должен возвращать** `TopoDS_Shape` (или `TDF_Label`), и `DisplaySketchLine` использует тот же shape.

**Оценка:** 30 минут (менять сигнатуру).

---

---

### CR-147. `maxTag`-цикл — O(N) на каждый клик
**Включает (merged):** CR-092, CR-093, CR-094, CR-137, CR-241, CR-250, CR-562, CR-625

**Где:** `Application.cpp:762-766`

```
int maxTag = 0;
TDF_ChildIterator it(myActiveSketchLabel, false);
for (; it.More(); it.Next()) {
    if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
}
TDF_Label lineLabel = myActiveSketchLabel.FindChild(maxTag, false);
```

**Проблемы:**
1. O(N) на каждый клик — при 1000 линиях в эскизе тормозит.
2. `FindChild(maxTag, false)` — флаг `false` = не создавать если нет. Но maxTag **только что найден** — должен существовать. А если `AddSketchLine` создал label с **другим** tag (нативный счётчик OCAF) — `FindChild` вернёт Null.
3. `myOcctView->DisplaySketchLine(line, lineLabel, ...)` — если `lineLabel.IsNull()`, что произойдёт?

**Фикс:** `AddSketchLine` возвращает созданный label. Убрать весь блок.

**Оценка:** входит в CR-142.

---

---

### CR-232. `AddCylinderFeature` и `AddSketchFeature` — БЕЗ транзакции OCAF
**Включает (merged):** CR-233, CR-234

**Где:** `BreezeDocument.cpp:220-235, 237-256`

**Проблема:**
```
TDF_Label BreezeDocument::AddCylinderFeature(...) {
    ...
    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);
    TDataStd_Name::Set(label, Utf8ToExt(name));    // ← без транзакции
    TDataStd_Integer::Set(label, FT_Cylinder);     // ← без транзакции
    TNaming_Builder(label).Generated(cyl);         // ← без транзакции
    myModified = true;
    return label;
}
```

**Не вызывается `StartTransaction` / `CommitTransaction`.** Все изменения идут вне OCAF-транзакции.

**Что ломается:**
1. `Undo()` после AddCylinder — откатывает **предыдущую** транзакцию (AddBox), а цилиндр **остаётся**. Пользователь видит странное поведение.
2. Документ считается modified (`myModified = true` — вручную), но история undo **не содержит** цилиндра.
3. При `SaveDocument` — сохраняется корректно (OCAF пишет всё), но при Undo после Load — цилиндр не откатится (нет транзакции).

**`AddSketchFeature`** — та же проблема **плюс**:
- **НЕ ставит `myModified = true`** → документ не считается изменённым.
- Пользователь создаёт эскиз, выходит → его **не спрашивают** о сохранении.
- При выходе эскиз теряется **без предупреждения**.

**Фикс:**
```
TDF_Label BreezeDocument::AddCylinderFeature(...) {
    ...
    StartTransaction();
    try {
        TDF_Label label = ...;
        TDataStd_Name::Set(...);
        TDataStd_Integer::Set(...);
        TNaming_Builder(label).Generated(cyl);
        CommitTransaction();
        return label;
    } catch (...) {
        // abort: myDoc->AbortCommand();
        throw;
    }
}
```

**Оценка:** 30 минут + проверка что Undo работает для всех типов.

---

---

### CR-235. `AddSketchLine` — теги переиспользуются после удаления → порядок ломается

**Где:** `BreezeDocument.cpp:270-271`

```
int tag = 1;
while (!sketchLabel.FindChild(tag, false).IsNull()) tag++;
```

**Сценарий:**
1. Создаём линии: tags 1, 2, 3, 4, 5.
2. Удаляем линии 2 и 4.
3. Создаём новую → `while` находит tag 2 (свободный) → новая линия получает **tag 2**.
4. `RemoveLastSketchLine` использует `maxTag` (5) → удаляет tag 5, а не новую (tag 2).

**Плюс:** при обходе дерева линии идут в порядке тегов: 1, 2(новая), 3, 5 — новая линия **в середине**.

**Плюс:** при Undo/Redo — восстанавливается по тегам, порядок может стать хаотичным.

**Фикс:** использовать монотонный счётчик (`myNextSketchLineId` в BreezeDocument или в самой sketch) или `myNextFeatureId`-подобный.

**Оценка:** 20 минут.

---

---

### CR-236. `RemoveFeature` — `ForgetAllAttributes` не очищает дочерние метки рекурсивно
**Включает (merged):** CR-242, CR-243

**Где:** `BreezeDocument.cpp:330-350`

```
label.ForgetAllAttributes(true);
```

**По документации OCAF:** `ForgetAllAttributes(Standard_Boolean theOnlyClosed)` очищает атрибуты **только текущей метки**. Дочерние метки **не затрагиваются**.

**Что это значит:**
- Удаляем `Sketch1` → её линии (дети) **остаются** в OCAF.
- Они не отображаются в дереве (родителя нет), но память занята.
- При сохранении — они пишутся в файл.
- **Утечка данных** в долгой сессии.

**Проверить:** `label.ForgetAllAttributes(true)` в коде OCAF.

**Фикс:**
```
void ForgetRecursively(const TDF_Label& label) {
    std::vector<TDF_Label> children;
    TDF_ChildIterator it(label, false);
    for (; it.More(); it.Next()) children.push_back(it.Value());
    for (auto& c : children) ForgetRecursively(c);
    label.ForgetAllAttributes(true);
}
```

**Оценка:** 30 минут + проверка через Load/Save.

---

---

### CR-238. `SetDocument` — комментарий про Origin неверный

**Где:** `BreezeDocument.cpp:46-49`

```
// Найти существующий Origin (тег 1)
if (!myDoc.IsNull()) {
    TDF_Label candidate = myDoc->Main().FindChild(1, false);
```

`Main().FindChild(1)` — это **Start** (тег 1 в `Main`), а не Origin (Origin — тег 1 в `Start`). Комментарий врёт.

**Фикс:** исправить комментарий: `// Найти Start (тег 1 под Main)`.

**Оценка:** 2 минуты.

---

---

### CR-239. `Has3DOperations` — хардкод 3 типов, не расширяется

**Где:** `BreezeDocument.cpp:56-63`

```
if (type == FT_Box || type == FT_Cylinder || type == FT_Hole) {
    return true;
}
```

При добавлении Extrude / Revolve / Sweep — забыть добавить сюда. Функция вернёт `false`, оси/плоскости останутся видимыми, UI сломается.

**Фикс:** либо `Is3DOperation(int type)` с явным switch, либо массив типов, либо **проверять семантически** (есть ли у фичи `TopoDS_Solid` или `TopoDS_Compound` в B-rep).

**Оценка:** 15 минут.

---

---

### CR-240. `RefreshNextFeatureId` — нет `IsNull()` guard, лог в stdout

**Где:** `BreezeDocument.cpp:65-75`

```
void BreezeDocument::RefreshNextFeatureId() {
    if (myDoc.IsNull()) return;
    int maxTag = 0;
    TDF_ChildIterator it(myDoc->Main(), false);
    for (; it.More(); it.Next()) {
        if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
    }
    myNextFeatureId = maxTag + 1;
    std::cout << "BreezeDocument: next feature id = " << myNextFeatureId << "\n";
}
```

**Проблемы:**
1. `std::cout` на каждый Load.
2. Логика: `maxTag` считается только по **прямым** детям Main. Если пользователь создал вложенные фичи (будущее), они не учитываются.
3. `maxTag + 1` — если maxTag = INT_MAX — переполнение. Теоретически.

**Оценка:** 5 минут + переход на TRACE.

---

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

---

### CR-268. `OcctView.h` — `GetAllLabels()` возвращает `vector` по значению

**Где:** `OcctView.h:80-85`

Каждый `TDF_Label` — refcount inc/dec. Вызывается в панелях для изоляции.

**Фикс:** `const std::vector<...>&` или span/итераторный интерфейс.

**Оценка:** 15 минут.

---

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

---

### CR-736. 🟠 `Localization.h` — inline `Tr()` в заголовке — экспорт

**Где:** `Localization.h:21-23`

```
inline std::string Tr(const std::string& key) {
    return Localization::Instance().Tr(key);
}
```

**Inline в хедере — ок**, но:
- `Tr` вызывается из **UI-слоя**, а `Localization` — в **app-слое**.
- Значит, **UI зависит от app** — по слоям верно (ui → app).
- Но **обратная зависимость app → ui** тоже есть (`Application.cpp` → `UI::Home`). **Цикл.**

**Связано с архитектурой.** Не критично, но стоит помнить.

**WONTFIX.**

---

## 🔴 P0 — doc (Волна 16, часть 6: BreezeApplication)

---

### CR-737. 🔴 `BreezeApplication::CreateDocument` — возвращает `nullptr` вместо `Handle()`

**Где:** `BreezeApplication.cpp:24-34`
**Статус:** OPEN

```
Handle(TDocStd_Document) BreezeApplication::CreateDocument() {
    Handle(TDocStd_Document) doc;
    TCollection_ExtendedString format("BinOcaf");

    myApp->NewDocument(format, doc);
    if (doc.IsNull()) {
        std::cerr << "BreezeApplication: failed to create new document\n";
        return nullptr;   // ← nullptr конвертируется в Handle?
    }
    return doc;
}
```

**Что не так:**
- `Handle(TDocStd_Document)` — это `opencascade::handle<TDocStd_Document>`.
- У него есть конструктор из `std::nullptr_t` → **создаёт пустой handle**. **Ок.**
- Формально работает, но: **читателю непонятно**. Лучше `return Handle(TDocStd_Document)();`.

**Фикс:** `return {};` или явный null-handle.

**Оценка:** 2 минуты.

---

---

### CR-738. 🔴 `BreezeApplication::SaveDocument` — `TCollection_ExtendedString(path.c_str())` — БАГ КИРИЛЛИЦЫ

**Где:** `BreezeApplication.cpp:49`
**Статус:** OPEN
**Категория:** тот же класс, что BUG-A5, но в doc-слое

```
TCollection_ExtendedString extPath(path.c_str());
```

**Что это значит:**
- `path` — `std::string` (UTF-8).
- `TCollection_ExtendedString(const char*)` — **конструктор от ANSI-строки** (CP1251 на русской Windows) **или** от UTF-8 в зависимости от OCCT-конфигурации.
- По умолчанию — **CP1251**. **Кириллица сломается** → это **BUG-A5 в doc-слое**.

**Плюс:** файл сохранится с кракозябрами в пути, но **файл не найдётся** при открытии (сравнение строк).

**Связано с TECH-DEBT-002** («кириллица в пути SaveAs → кракозябры») — **тот же источник**.

**Фикс:**
```
TCollection_ExtendedString extPath = Util::Utf8ToExt(path);
```

Но это **зависит от CR-679** (кроссплатформенный баг Utf8).

**Оценка:** 5 минут после CR-679.

---

---

### CR-739. 🔴 `BreezeApplication::OpenDocument` — та же проблема

**Где:** `BreezeApplication.cpp:60`

```
TCollection_ExtendedString extPath(path.c_str());
```

**Связано с CR-738.**

**Плюс:** `PCDM_ReaderStatus status` — статус читается, но в лог пишется **числом** (`status=`), не human-readable. Сложно диагностировать.

**Фикс:** либо таблица статусов, либо `PCDM_ReaderStatusToString(status)`.

**Оценка:** входит в CR-738.

---

---

### CR-742. 🟠 `BreezeApplication` — `myApp = new TDocStd_Application()` — не `Handle`

**Где:** `BreezeApplication.cpp:16`

```
myApp = new TDocStd_Application();
```

`myApp` — `Handle(TDocStd_Application)`. `new` возвращает `TDocStd_Application*`, а Handle принимает его **с ++refcount** — ок.

**WONTFIX.**

---

---

### CR-743. 🟠 `BreezeApplication` — `TCollection_ExtendedString format("BinOcaf")` — hardcoded формат

**Где:** `BreezeApplication.cpp:26`

**Фикс:** `constexpr const char* kOcafFormat = "BinOcaf";`.

**Оценка:** 2 минуты.

---

---

### CR-745. 🟠 `BreezeApplication` — `SaveDocument` использует `myApp->SaveAs`, а не свой формат

**Где:** `BreezeApplication.cpp:52`

```
myApp->SaveAs(doc, extPath);
```

**По `FILE_FORMAT.md`** — целевой формат `.bzc` (ZIP + manifest + history). Здесь — **OCAF `BinOcaf`** (`.cbf`).

**Связано с ADR-0001** (план: свой формат).

**Отложено до Фазы 3.**

---

---

