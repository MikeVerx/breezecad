# RAM — гипотезы и диагностика — часть 1 из 1

**Записей в части:** 13

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---


**Симптом (автор):** старт ~100 MB, через час работы ~150 MB и продолжает расти.

**Гипотезы:**


### RAM-H1. Кэш текстур иконок в `IconManager` 🔴

**Подозрение:** `IconManager::Get(name, size)` кэширует текстуры по паре (имя, размер). Но:
- Кэш **никогда не чистится** — если размер меняется (zoom/resize), накапливаются новые текстуры.
- GL-текстуры (`GLuint`) не удаляются — утечка GPU RAM.

**Проверить:**
```
grep -an "glDeleteTextures\|~IconManager\|ClearCache" src/app/IconManager.*
```

**Вероятность:** 🔴 **высокая.**

---


### RAM-H2. `IconManager` держит все SVG в памяти 🔴

**Подозрение:** 105 иконок предзагружены (лог: `IconManager: preloaded 105/105`). Возможно, держит и исходные SVG-буферы, и растеризованные RGBA.

**Проверить:**
```
grep -an "mySvgBuffers\|myRawData\|myCache" src/app/IconManager.h
```

**Вероятность:** 🟠 средняя.

---


### RAM-H3. Кэш превью/миниатюр 🔴

**Подозрение:** `myThumbnailPaths` и `Thumbnails.cpp` — каждый проект держит PNG в памяти. Если много документов открыто/закрыто — накапливаются.

**Проверить:**
```
grep -an "myThumbnail\|CaptureCurrentThumbnail" src/app/Application.cpp | head -20
```

**Вероятность:** 🟠 средняя.

---


### RAM-H4. OCCT AIS_InteractiveObject — не освобождаются

**Подозрение:** каждый `AddBox`/`DisplaySketchLine` создаёт AIS-объект в `OcctView`. Если документ закрыт, но AIS не очищен — OCCT держит.

**Проверить:**
```
grep -an "myContext->Remove\|AIS_InteractiveContext::Remove\|myContext->Erase" src/occt/OcctView.cpp
```

**Вероятность:** 🔴 **высокая.**

---


### RAM-H5. `std::function` closure в `DrawRibbon` — накапливаются

**Связь с CR-001.** Каждый кадр — новые closures. Уничтожаются в конце кадра, но heap-фрагментация растёт.

**Вероятность:** 🟡 низкая (фрагментация, не рост).

---


### RAM-H6. `TRACE` пишет в файл с `fflush` каждый кадр

**Подозрение:** даже при `BREEZECAD_TRACE=0` — мёртвый код? Проверить.

**Вероятность:** 🟢 низкая.

---


### RAM-H7. `myCommands` — вектор не чистится

**Подозрение:** `InitCommands` вызывается один раз? Или каждый doc?

**Проверить:**
```
grep -an "InitCommands" src/app/Application.cpp
```

**Вероятность:** 🟡 низкая.

---


### RAM-H8. Утечка в ImGui — drawList не сбрасывается

**Подозрение:** если какой-то `Begin*` без парного `End*` — ImGui копит.

**Проверить визуально:** в debug-сборке ImGui выдаёт assert.

**Вероятность:** 🟡 низкая (упало бы).

---


### RAM-H9. Кэш `std::map<std::string, bool> myVisibility` растёт

**Подозрение:** в `myVisibility` — записи для всех фич. Если фичи удаляются, а записи остаются — медленный рост.

**Проверить:**
```
grep -an "myVisibility\[\|myVisibility.erase\|myVisibility.clear" src/app/Application.cpp
```

**Вероятность:** 🟠 средняя.

---


### RAM-H10. OCAF документы при закрытии не освобождаются

**Подозрение:** `CloseDoc` удаляет из `myDocuments`, но `Handle(TDocStd_Document)` может держаться где-то ещё (например, в `BreezeApplication`).

**Проверить:**
```
grep -an "mySession->Close\|mySession->NewDocument\|myDocuments.erase\|myDocuments.pop_back" src/app/Application.cpp
```

**Вероятность:** 🔴 **высокая.**

---

**План диагностики:**

1. **Замер базовый:** запустить, `tasklist | grep -i breezecad` каждые 30 сек — записать в файл.
2. **Сценарий А:** открыть-закрыть 10 документов — RAM?
3. **Сценарий Б:** нарисовать 100 линий в эскизе, выйти-войти — RAM?
4. **Сценарий В:** покрутить 3D 5 минут, вернуться в idle — RAM?
5. **Сценарий Г:** оставить в idle 30 минут без действий — RAM?

**Ожидаемые результаты:**
- Если растёт в idle → **утечка в draw-path** (RAM-H5, H6).
- Если растёт после сценария А → **OCAF/documents не освобождаются** (RAM-H10).
- Если растёт после сценария Б → **OCAF/AIS** (RAM-H4, H10).
- Если растёт после В → **OCCT V3d** (RAM-H4).

**Замер инструмент:** в ImGui уже есть FPS/RAM-телеметрия (`UpdateTelemetry`). Она читает WinAPI `GetProcessMemoryInfo` — точная.

**Отдельный файл:** `docs/RAM_AUDIT.md` — создать, когда начнём копать. Здесь только подозрения.

**Приоритет:** 🟠 **высокий.** Это нарушает философию MVP («RAM <500 МБ на сборке 200 деталей, idle 60 FPS»). Если idle растёт — MVP не получится.

**Порядок работ:**
1. Сначала CR-001 (убрать closures) — может уже дать −50% роста.
2. Потом RAM-H1 (кэш иконок) — если размер меняется.
3. Потом RAM-H4/H10 (OCCT/OCAF).
4. Диагностика сценариями.

---



### RAM-H10 [ПОДТВЕРЖДЕНО косвенно]

**`~BreezeDocument` = default** — не закрывает документ через OCAF-сессию. Если `BreezeApplication` не отслеживает и не закрывает — `TDocStd_Document` остаётся в сессии до конца процесса.

**Требует проверки:** `BreezeApplication::CloseDocument` — вызывается ли на каждый `~BreezeDocument`?

**Проверить в:** `src/doc/BreezeApplication.cpp` (следующий файл).

---


### RAM-H4 [НЕ ПОДТВЕРЖДЕНО здесь]

`BreezeDocument` **не держит GL-ресурсов** — только OCAF + TopoDS_Shape. `RAM-H4` — про `OcctView` (следующий файл).

---


