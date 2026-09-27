# BreezeCAD — Review: P0 / iconman

**Записей:** 10

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-091. `IconManager::LoadAll("assets/icons")` — хардкод пути
**Включает (merged):** CR-219, CR-335, CR-457

**Где:** `Application.cpp:140`
RULES п.23: «Никаких хардкодов путей. `std::filesystem` или glfw-хелперы.»

**Фикс:** `std::filesystem::path exeDir = ...` + `exeDir / "assets" / "icons"`.

---

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

---

### CR-225. `IconManager` — `RasterizeToTexture` вызывается без проверки активного GL-контекста

**Где:** `IconManager.cpp:110-175`

`glGenTextures`, `glBindTexture`, `glTexImage2D` требуют **активного GL-контекста**. Если `Get` вызван до `makeContextCurrent` или после `glfwTerminate` — GL-ошибка или краш.

**Фикс:** `assert(glfwGetCurrentContext() != nullptr);` или проверка в `Get`.

**Оценка:** 5 минут.

---

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

---

### RAM-H1. Кэш текстур иконок в `IconManager` 🔴
**Включает (merged):** RAM-H3, CR-110, CR-224

**Подозрение:** `IconManager::Get(name, size)` кэширует текстуры по паре (имя, размер). Но:
- Кэш **никогда не чистится** — если размер меняется (zoom/resize), накапливаются новые текстуры.
- GL-текстуры (`GLuint`) не удаляются — утечка GPU RAM.

**Проверить:**
```
grep -an "glDeleteTextures\|~IconManager\|ClearCache" src/app/IconManager.*
```

**Вероятность:** 🔴 **высокая.**

---

---

