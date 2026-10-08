# BreezeCAD — Review: P0 / cmake

**Записей:** 14

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-196. `CMakeLists.txt` — `TKCAF TKLCAF TKBin` всё ещё линкуются

**Где:** `CMakeLists.txt:135`
```
TKCAF TKLCAF TKBin
```

**По ADR-0001:** OCAF должен быть **модулем-мостом**, не в базе. Сейчас эти три DLL входят в поставку → лишний вес.

**Фикс:** исключить после перехода на свой `.bzc`. **Сейчас** — задокументировать в CODE_REVIEW как TECH-DEBT.

**Оценка:** после Фазы 3.

---

---

### CR-197. `CMakeLists.txt` — `CMAKE_CXX_FLAGS_RELEASE` перетирает дефолт

**Где:** `CMakeLists.txt:14-15`
```
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
```

**Проблема:** эти переменные CMake устанавливает **сам** со своими значениями. Перезапись через `set` **стирает** всё, что туда добавили бы внешние модули (`OpenCASCADE`, `glfw`).

**Фикс:**
```
target_compile_options(breezecad PRIVATE
    $<$<CONFIG:Release>:-O2>
)
```

**Оценка:** 20 минут.

---

---

### CR-198. `CMakeLists.txt` — нет `-Wall -Wextra -Wpedantic`

**Где:** весь файл
Связано с CR-023. **Фикс:**
```
target_compile_options(breezecad PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unused-parameter>  # или включать всё
)
```

**Оценка:** 30 минут + правки существующих warnings.

---

---

### CR-199. `CMakeLists.txt` — `include_directories` глобальный, deprecated

**Где:** `CMakeLists.txt:21, 33`
```
include_directories(${LUNASVG_ROOT}/include)
...
include_directories(${OpenCASCADE_INCLUDE_DIR})
```

Глобальный `include_directories` устарел. **Фикс:** `target_include_directories(... SYSTEM PRIVATE ...)`.

**Оценка:** 10 минут.

---

---

### CR-200. `CMakeLists.txt` — нет `-flto` для Release

**Цель проекта:** вес 200-500 МБ. Без LTO — не достижимо. `-flto` режет размер exe на 10-30%.

**Фикс:**
```
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok)
if(ipo_ok)
    set_property(TARGET breezecad PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
```

**Оценка:** 15 минут + проверка линковки с OCCT.

---

---

### CR-201. `CMakeLists.txt` — нет `strip` для Release

**Где:** весь файл
Финальный exe содержит отладочные символы → на 20-40% больше. **Фикс:** `-s` или `objcopy --strip-all`.

**Оценка:** 10 минут.

---

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

---

### CR-203. `CMakeLists.txt` — `project(BreezeCAD LANGUAGES CXX)` без VERSION

**Где:** `CMakeLists.txt:2`

Нет версии. Нет `description`. Для CI, дистрибутивов, `--version` в бинарнике — нужно.

**Фикс:** `project(BreezeCAD VERSION 0.1.0 LANGUAGES CXX DESCRIPTION "...")`.

**Оценка:** 5 минут.

---

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

---

### CR-205. `CMakeLists.txt` — `add_library(imgui)` включает `imgui_demo.cpp` в релиз

**Где:** `CMakeLists.txt:38`

`imgui_demo.cpp` — **демо-код**, не нужен в продакшене. Плюс ~50 КБ.

**Фикс:** `#ifdef BREEZECAD_DEBUG` или отдельная статическая либа.

**Оценка:** 10 минут.

---

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

---

### CR-207. `CMakeLists.txt` — нет `CMAKE_EXPORT_COMPILE_COMMANDS`

**Где:** весь файл

Без `compile_commands.json` не работает clang-tidy, clangd, многие IDE. **Фикс:** `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`.

**Оценка:** 1 минута.

---

---

### CR-208. `CMakeLists.txt` — нет `CMAKE_POSITION_INDEPENDENT_CODE`

**Где:** весь файл

Для будущей сборки плагинов (Фаза 5+) и shared library — нужно. **Фикс:** `set(CMAKE_POSITION_INDEPENDENT_CODE ON)`.

**Оценка:** 1 минута.

---

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

---

