# P0 — КРИТИЧНО — часть 3 из 5

**Записей в части:** 32

**Легенда статуса:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-201. `CMakeLists.txt` — нет `strip` для Release

**Где:** весь файл
Финальный exe содержит отладочные символы → на 20-40% больше. **Фикс:** `-s` или `objcopy --strip-all`.

**Оценка:** 10 минут.

---


### CR-202. `CMakeLists.txt` — нет `CMAKE_CXX_EXTENSIONS OFF`

**Где:** `CMakeLists.txt:4`
```
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# ← нет EXTENSIONS
```

GCC по умолчанию использует GNU-расширения. Для переносимости (ARM, macOS) — отключить.

**Фикс:** `set(CMAKE_CXX_EXTENSIONS OFF)`.

**Оценка:** 2 минуты.

---


### CR-203. `CMakeLists.txt` — `project(BreezeCAD LANGUAGES CXX)` без VERSION

**Где:** `CMakeLists.txt:2`

Нет версии. Нет `description`. Для CI, дистрибутивов, `--version` в бинарнике — нужно.

**Фикс:** `project(BreezeCAD VERSION 0.1.0 LANGUAGES CXX DESCRIPTION "...")`.

**Оценка:** 5 минут.

---


### CR-204. `CMakeLists.txt` — OCCT-библиотеки захардкожены

**Где:** `CMakeLists.txt:129-135`

```
TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
TKPrim TKTopAlgo TKBO
TKDESTEP
TKDESTL
TKV3d TKOpenGl TKService TKMesh
TKCAF TKLCAF TKBin
```

`find_package(OpenCASCADE)` уже заполняет `${OpenCASCADE_LIBRARIES}`. Хардкод — brittle при обновлении OCCT.

**Фикс:** либо `${OpenCASCADE_LIBRARIES}`, либо `${OpenCASCADE_LIBRARIES} TKCAF TKLCAF TKBin` (добавки).

**Оценка:** 15 минут.

---


### CR-205. `CMakeLists.txt` — `add_library(imgui)` включает `imgui_demo.cpp` в релиз

**Где:** `CMakeLists.txt:38`

`imgui_demo.cpp` — **демо-код**, не нужен в продакшене. Плюс ~50 КБ.

**Фикс:** `#ifdef BREEZECAD_DEBUG` или отдельная статическая либа.

**Оценка:** 10 минут.

---


### CR-206. `CMakeLists.txt` — `POST_BUILD copy_directory` OCCT-DLL копирует **все** DLL

**Где:** `CMakeLists.txt:157-161`

```
add_custom_command(TARGET breezecad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${OCCT_ROOT}/win64/gcc/bin"
    "$<TARGET_FILE_DIR:breezecad>"
)
```

Копирует **всю папку** — включая DLL, которые не линкуются. Это **десятки мегабайт мусора** в portable-сборке.

**Плюс:** в проекте **уже есть** `scripts/deploy-runtime.sh`, который делает правильный BFS-анализ через objdump. Но он **не вызывается** из CMake.

**Фикс:** вызвать `deploy-runtime.sh` после сборки, а не `copy_directory`.

**Оценка:** 20 минут.

---


### CR-207. `CMakeLists.txt` — нет `CMAKE_EXPORT_COMPILE_COMMANDS`

**Где:** весь файл

Без `compile_commands.json` не работает clang-tidy, clangd, многие IDE. **Фикс:** `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`.

**Оценка:** 1 минута.

---


### CR-208. `CMakeLists.txt` — нет `CMAKE_POSITION_INDEPENDENT_CODE`

**Где:** весь файл

Для будущей сборки плагинов (Фаза 5+) и shared library — нужно. **Фикс:** `set(CMAKE_POSITION_INDEPENDENT_CODE ON)`.

**Оценка:** 1 минута.

---


### CR-209. `CMakeLists.txt` — `${LUNASVG_ROOT}/lib/liblunasvg.a` — прямой путь вместо `find_library`

**Где:** `CMakeLists.txt:141-142`

```
${LUNASVG_ROOT}/lib/liblunasvg.a
${LUNASVG_ROOT}/lib/libplutovg.a
```

Жёстко привязано к статической lib. На Linux/Mac будут `.so`/`.dylib`. **Фикс:** `find_library(LUNASVG_LIB lunasvg PATHS ${LUNASVG_ROOT}/lib ...)`.

**Оценка:** 10 минут.

---



### RAM-H1 [ПОДТВЕРЖДЕНО]. `~IconManager()` отсутствует — утечка GL-текстур

**Где:** `IconManager.h:13-30`, `IconManager.cpp:250-255`

**Проблема:**
```
class IconManager {
public:
    void LoadAll(...);
    void PreloadAll(...);
    GLuint Get(...);
    void Clear();
private:
    std::unordered_map<std::string, GLuint> myCache;
    // ← НЕТ ~IconManager()
};

void IconManager::Clear() {
    for (auto& kv : myCache) {
        if (kv.second) glDeleteTextures(1, &kv.second);
    }
    myCache.clear();
}
```

`Clear()` есть, но **никто не вызывает**. `~IconManager` = default → `myCache` очищается без `glDeleteTextures`. **Каждая GL-текстура остаётся в VRAM до конца процесса.**

**Цифры:**
- 105 иконок × 128×128 × 4 байта = **6.7 МБ GPU RAM**.
- Если Application создаётся/уничтожается (тесты, многооконка) — **6.7 МБ × N**. Утечка.

**Приложение сейчас живёт одним Application.** Но при `DestroyContext` (в ~Application) без `Clear()` — **утечка гарантирована**.

**Фикс:**
```
class IconManager {
public:
    ~IconManager() { Clear(); }
    // ...
};
```

Или (лучше) — не полагаться на неявный вызов, а **явно** звать `myIcons.Clear()` в `~Application` перед `ImGui_ImplOpenGL3_Shutdown`.

**Оценка:** 5 минут.

---


### RAM-H1a [НОВОЕ]. `RasterizeToTexture` рендерит в `size * 4` — в 16 раз больше нужного

**Где:** `IconManager.cpp:135`

```
int renderSize = size * 4;  // x4 для HiDPI и векторной чёткости
auto bitmap = doc->renderToBitmap(renderSize, renderSize);
```

Для `size = 32` (запрос) → рендер 128×128. **В 16 раз больше пикселей**, чем нужно для отображения в 32 px.

**Обоснование в комментарии** — «x4 для HiDPI». Но:
- Все иконки в ribbon запрашиваются с `size = 32` или `24` — на **реальном экране** они показываются как 32 CSS-px.
- На HiDPI (2x) — реальный рендер 64×64 — достаточно.
- **x4 = 128×128 избыточно вдвое.**

**Цифры:**
- Текущий размер: 105 × 128×128 × 4 = **6.7 МБ GPU**.
- С x2: 105 × 64×64 × 4 = **1.7 МБ**.
- С x1 (ровно 32): 105 × 32×32 × 4 = **0.4 МБ**.

**Разница — в 16 раз.** Для проекта с философией «4 ГБ, iGPU, ARM» — существенно.

**Фикс:**
```
// x2 для HiDPI (достаточно для Retina 2x), но не больше
int renderSize = size * 2;
```
И — **зависит от DPI монитора**: на обычном DPI = `size`, на HiDPI = `size * 2`. Брать из `ImGui::GetIO().DisplayFramebufferScale`.

**Оценка:** 20 минут.

---


### RAM-H1b [НОВОЕ]. `RasterizeToTexture` — `bitmap` держит RGBA в heap после создания текстуры

**Где:** `IconManager.cpp:159-170`

```
std::vector<uint8_t> rgba(w * h * 4);   // 128*128*4 = 65 KB на иконку
...
return CreateTexture(rgba.data(), w, h);
```

`std::vector` локальный, удалится при выходе из функции. **Но:** `glTexImage2D` копирует данные в GPU **синхронно** — если драйвер не дожидается, может быть use-after-free. На практике ок (glTexImage2D синхронный по спецификации), но стоит явно `glFinish()` или удалить `bitmap` до `glTexImage2D`.

**Дополнительно:** `bitmap` от lunasvg **тоже держит RGBA** — то есть в момент конвертации **два буфера** в памяти (source + dest) по 65 KB каждый.

**Фикс:** делать конвертацию **in-place** — lunasvg возвращает неконстантный указатель? Если да — можно мутировать.

**Оценка:** 15 минут.

---


### RAM-H2 [НЕ ПОДТВЕРЖДЕНО]. Иконки не держат исходные SVG в памяти

`LoadAll` только сохраняет путь. `RasterizeToTexture` читает SVG, парсит, рендерит и **освобождает** всё. **Никакого накопления SVG-буферов нет.** Гипотеза снимается.

---


### CR-217. `IconManager::LoadAll` — название вводит в заблуждение

**Где:** `IconManager.cpp:193-202`

```
void IconManager::LoadAll(const std::string& dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(dir)) { ... }
    myIconDir = dir;
    std::cout << "IconManager: icons dir = " << dir << "\n";
}
```

**Ничего не грузит.** Только сохраняет путь. Реальная загрузка — в `PreloadDefaults`. **Фикс:** переименовать в `SetIconDir` (уже есть метод!) или объединить.

**Оценка:** 5 минут.

---


### CR-218. `IconManager::Get` — `size` параметр игнорируется при рендере

**Где:** `IconManager.cpp:230`

```
GLuint tex = RasterizeToTexture(foundPath, 32);  // ← ВСЕГДА 32, а не size
```

**Функциональный баг.** `Get(name, 64)` вернёт текстуру, отрендеренную для 32 (потом масштабируется). Кэш по ключу `name#64` — но сама текстура — 32.

**Что это значит:**
- Логотип проекта 280 px → `Get("logo", 280)` → рендер 32×32 → **размытое пятно**.
- В комментарии к `Get` написано «Позволяет держать и мелкие иконки (32), и крупные (лого 280+) в правильном разрешении без размытия.» — **это ложь**, код этого не делает.

**Фикс:** `RasterizeToTexture(foundPath, size)`.

**Оценка:** 2 минуты.

---


### CR-219. `IconManager` — хардкод путей (`assets/icons/`, `../assets/icons/`)

**Где:** `IconManager.cpp:217-222`

```
std::vector<std::string> candidates = {
    myIconDir + "/" + realName + ".svg",
    "assets/icons/" + realName + ".svg",
    "../assets/icons/" + realName + ".svg",
    "../../assets/icons/" + realName + ".svg",
};
```

RULES п.23: «Никаких хардкодов путей.» Здесь четыре хардкода подряд.

**Фикс:** `std::filesystem::path exeDir = ...` + `exeDir / "assets" / "icons"`.

**Оценка:** 15 минут.

---


### CR-220. `IconManager::CreateTexture` — нет проверки `glGenTextures` error

**Где:** `IconManager.cpp:65-75`

```
GLuint texId = 0;
glGenTextures(1, &texId);
// ← нет проверки texId != 0
glBindTexture(GL_TEXTURE_2D, texId);
```

Если `glGenTextures` вернёт 0 (out of memory), дальше `glBindTexture(0)` — unbind, `glTexImage2D` уйдёт в никуда. **Тихая деградация.**

**Фикс:** `if (texId == 0) { std::cerr << "IconManager: glGenTextures failed\n"; return 0; }`.

**Оценка:** 3 минуты.

---


### CR-221. `IconManager::RasterizeToTexture` — `while ((pos = svgData.find("currentColor", pos)) != npos)`

**Где:** `IconManager.cpp:118-122`

```
size_t pos = 0;
while ((pos = svgData.find("currentColor", pos)) != std::string::npos) {
    svgData.replace(pos, 12, "#FFFFFF");
    pos += 7;
}
```

**Логика корректна** (`+7` = длина `#FFFFFF`), но:
- **O(n²)** на большом SVG с N заменами (каждый `replace` сдвигает хвост).
- Если бы `#FFFFFF` был короче `currentColor` — бесконечный цикл (здесь не так).

**Фикс:** один проход с `find` + `append` в новый буфер. Или если замена одноразовая — `.replace(pos, 12, "#FFFFFF")` и **не сдвигать pos** (всё равно следующий find начнёт с той же позиции).

**Оценка:** 10 минут.

---


### CR-222. `IconManager::Get` — ленивая загрузка vs комментарий «предзагрузка»

**Где:** `IconManager.cpp:204-235`

Комментарий в `LoadAll`: «Предзагрузка всех иконок при старте (чтобы не тормозить первый кадр)». Но `LoadAll` ничего не грузит, а `Get` — ленивый. Значит, **первый кадр** всё равно тормозит на **первый** запрос каждой иконки.

**PreloadDefaults** частично решает — грузит 105 штук. Но:
- Грузит **все** 105 в GPU — даже те, что не видны.
- Если что-то в `Get` вызывается до `PreloadDefaults` — блок.

**Фикс:** либо явно грузить только видимое (по активной вкладке), либо отложить загрузку в фоновый поток (std::async).

**Оценка:** 30 минут.

---


### CR-223. `IconManager` — `PreloadDefaults` захардкожен список 105 иконок

**Где:** `IconManager.cpp:239-291`

```
static const std::vector<std::string> kDefaultIcons = { ... 105 items ... };
```

Если в UI добавится новая иконка, и её забудут в списке — при первом использовании будет lazy load (задержка). **Плюс:** 105 иконок — большой список, который надо поддерживать синхронно с кодом UI.

**Фикс:** либо генерировать список из `assets/icons/*.svg` автоматически, либо грузить по требованию с эвристикой «часто используемые — сразу, редкие — lazy».

**Оценка:** 30 минут.

---


### CR-224. `IconManager` — `myCache` без очистки — растёт от смены размера

**Где:** `IconManager.cpp:205-209`

```
std::string key = name + "#" + std::to_string(size);
auto it = myCache.find(key);
if (it != myCache.end()) return it->second;
...
myCache[key] = tex;
```

**Кэш никогда не чистится.** Если пользователь:
- Меняет масштаб UI → меняются размеры иконок → новые ключи → новые текстуры.
- Много разных размеров (`Get("icon", 16)`, `Get("icon", 24)`, `Get("icon", 32)`, `Get("icon", 48)`) → 4 текстуры на одну иконку.

**При 105 иконках × 4 размера = 420 текстур × 64×64×4 = 6.9 МБ GPU** — накопительно.

**Связано с RAM-H1.** **Фикс:** LRU-кэш с лимитом (например, 256 текстур).

**Оценка:** 30 минут.

---


### CR-225. `IconManager` — `RasterizeToTexture` вызывается без проверки активного GL-контекста

**Где:** `IconManager.cpp:110-175`

`glGenTextures`, `glBindTexture`, `glTexImage2D` требуют **активного GL-контекста**. Если `Get` вызван до `makeContextCurrent` или после `glfwTerminate` — GL-ошибка или краш.

**Фикс:** `assert(glfwGetCurrentContext() != nullptr);` или проверка в `Get`.

**Оценка:** 5 минут.

---


### CR-226. `IconManager` — `#ifdef _WIN32 #include <windows.h> #endif` — зачем?

**Где:** `IconManager.h:7-9`

```
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
```

`windows.h` подключается **перед** `gl.h` — это стандартный трюк Windows (иначе `gl.h` требует `windows.h` или `WINGDIAPI`). Но этот факт не задокументирован. **Фикс:** комментарий.

**Оценка:** 1 минута.

---



### CR-232. `AddCylinderFeature` и `AddSketchFeature` — БЕЗ транзакции OCAF

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


### CR-233. `AddSketchLine` — транзакция открыта при раннем `return`

**Где:** `BreezeDocument.cpp:266-289`

```
StartTransaction();

int tag = 1;
while (!sketchLabel.FindChild(tag, false).IsNull()) tag++;

TDF_Label lineLabel = sketchLabel.FindChild(tag, true);
TDataStd_Integer::Set(lineLabel, FT_SketchLine);

TDF_Label styleLabel = lineLabel.FindChild(100, true);
TDataStd_Integer::Set(styleLabel, static_cast<int>(style));

TopoDS_Shape edge = BRepBuilderAPI_MakeEdge(p1, p2).Shape();
if (edge.IsNull()) {
    std::cerr << "AddSketchLine: edge is null\n";
    CommitTransaction();   // ← вызывается! но edge не создан, зачем коммит?
    return TDF_Label();
}
TNaming_Builder(lineLabel).Generated(edge);
CommitTransaction();
```

**Проблемы:**
1. При `edge.IsNull()` → `CommitTransaction()` **завершает** транзакцию, хотя фича не создана. Мусор в истории undo.
2. При **исключении** в `TNaming_Builder` → catch → **возврат** без `CommitTransaction` / `AbortCommand`. `OpenCommand` остаётся открытым.
3. Следующий вызов `AddBoxFeature` → `StartTransaction` → `OpenCommand` **второй раз** на той же транзакции → тихое наложение.

**Фикс:**
- `AbortCommand` при ошибке.
- `try/catch` со сбросом транзакции.

**Оценка:** 20 минут.

---


### CR-234. `AddBoxFeature` — транзакция не закрывается при исключении

**Где:** `BreezeDocument.cpp:187-206`

```
StartTransaction();

TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);
TDataStd_Name::Set(label, Utf8ToExt(name));
TDataStd_Integer::Set(label, FT_Box);

gp_Pnt corner(px, py, pz);
TopoDS_Shape box = BRepPrimAPI_MakeBox(corner, dx, dy, dz).Shape();  // ← может бросить

TNaming_Builder(label).Generated(box);
CommitTransaction();  // ← НЕ выполнится при исключении
```

Если `dx/dy/dz < 0` — OCCT бросает `Standard_DomainError`. Транзакция остаётся открытой.

**Тот же паттерн в `RenameFeature` и `RemoveFeature`** — но там меньше шансов на исключение.

**Фикс:** RAII-guard для транзакции:
```
class OcafTransaction {
    Handle(TDocStd_Document) myDoc;
    bool myCommitted = false;
public:
    OcafTransaction(Handle(TDocStd_Document) d) : myDoc(d) { myDoc->OpenCommand(); }
    ~OcafTransaction() { if (!myCommitted) myDoc->AbortCommand(); }
    void Commit() { myDoc->CommitCommand(); myCommitted = true; }
};
```

**Оценка:** 40 минут.

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


### CR-236. `RemoveFeature` — `ForgetAllAttributes` не очищает дочерние метки рекурсивно

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


### CR-237. `~BreezeDocument` = default — RAM-H10-кандидат

**Где:** `BreezeDocument.h:51`

**Проблема:** `~BreezeDocument() = default;` — `myDoc` (Handle) декрементится. Но:
- `BreezeApplication::Instance().CreateDocument()` создаёт документ в **сессии** OCAF.
- Сессия `TDocStd_Application` **держит** документ, пока `CloseDocument` не вызван.
- Если `BreezeDocument` уничтожен **без** `CloseDocument` — OCAF-документ **остаётся в сессии**.

**Что это значит для RAM:** каждый `CloseDoc` в Application вызывается корректно (`BreezeApplication::CloseDocument`), но:
- `BreezeDocument` в конструкторе **создаёт** документ через `BreezeApplication::Instance().CreateDocument()`.
- Деструктор **не закрывает** документ.
- Если `BreezeDocument` создан, но не закрыт через сессию — утечка.

**Проверить:** `BreezeApplication::CreateDocument` и `CloseDocument`.

**Фикс:** в `~BreezeDocument` — если `myDoc` не Null и не был передан извне — закрыть через сессию.

**Оценка:** 20 минут (после проверки BreezeApplication).

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


