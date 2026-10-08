# BreezeCAD — Review: P0 / platform

**Записей:** 17

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-086. `Application` конструктор — молчаливый выход при провале окна

**Где:** `Application.cpp:135`
```
myWindow = std::make_unique<GlfwWindow>(...);
if (!myWindow->GetNativeWindow()) return;   // ← return из конструктора!
```

Если окно не создалось — конструктор завершается, но **объект создан в полу-состоянии**. `Run()` вызовется и упадёт.

**Фикс:** либо `throw std::runtime_error`, либо явное поле `bool myInitFailed`.

---

---

### CR-090. `~Application()` — порядок shutdown может дать UB

**Где:** `Application.cpp:199-203`
```
Application::~Application() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
```

**Проблемы:**
1. `myOcctView` — `unique_ptr`, dtor вызовется **после** тела деструктора. То есть **после** `ImGui shutdown` и `DestroyContext`. Если `OcctView::~OcctView()` дёргает `ImGui` или `ImGui` state — UB.
2. `myIcons` — не освобождает GL-текстуры. Утечка при перезапуске.
3. `myWindow` — тоже после. Если `GlfwWindow::~GlfwWindow()` дёргает `glfwTerminate()` — а `ImGui` уже сдох. Порядок критичен.

**Фикс:** явный сброс `myOcctView.reset(); myWindow.reset();` **в начале** деструктора, **до** ImGui shutdown.

---

---

### CR-713. 🟠 `FileDialog.h` — комментарий про `save = false/true` — нет

**WONTFIX** — норм.

---

## 🔴 P0 — platform (Волна 16, часть 2: GlfwWindow)

---

### CR-714. 🔴 `GlfwWindow` — Compatibility Profile НЕ установлен, комментарий врёт

**Где:** `GlfwWindow.cpp:19-22`
**Статус:** OPEN
**Категория:** потенциальный краш на macOS + рассинхрон с комментарием

```
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
// Не указываем Core Profile — по умолчанию будет Compatibility
// glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
```

**Комментарий врёт.** По документации GLFW:
- Если запросить **3.2+**, GLFW **по умолчанию даёт Core Profile**, а не Compatibility.
- На macOS **Compatibility недоступен вообще** для 3.2+ — только Core.
- На Windows/Linux — работает, но это неявное поведение.

**Почему важно:**
- **`OcctView::Init`** устанавливает `myDriver->ChangeOptions().contextCompatible = true;` (CR-257 — ага, у нас другой CR). Если реальный контекст — Core, **OCCT может не заработать** с толстыми линиями.
- Но в проекте **работает** — значит, либо GLFW даёт Compatibility, либо OCCT адаптируется.
- **На macOS** контекст 3.3 = **Core**, и толстые линии (`glLineWidth > 1`) **не работают** — deprecated.

**Фикс:**
```
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPATIBILITY_PROFILE);   // ← явно
```

**Плюс:** для macOS — `#ifdef __APPLE__` fallback на Core + custom line shader.

**Оценка:** 30 минут (Windows/Linux) + отдельная задача для macOS.

---

---

### CR-716. 🔴 `GlfwWindow::~GlfwWindow` — `glfwTerminate` убивает всю GLFW

**Где:** `GlfwWindow.cpp:57-62`

```
GlfwWindow::~GlfwWindow() {
    if (myWindow) glfwDestroyWindow(myWindow);
    glfwTerminate();   // ← терминейтит ВСЮ библиотеку
}
```

**Проблема:** если в проекте **два** `GlfwWindow` (например, в тестах или многооконном режиме), деструктор первого убьёт GLFW **до** деструктора второго → **краш во втором**.

**Фикс:** рефкаунтинг или — `glfwTerminate` только в `Application::~Application` (последний владелец).

**Оценка:** 20 минут.

---

---

### CR-717. 🔴 `GlfwWindow::GlfwWindow` — при провале `glfwInit` не сбрасывает `myWindow`

**Где:** `GlfwWindow.cpp:13-16`

```
GlfwWindow::GlfwWindow(...) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return;   // ← myWindow остался nullptr (ок)
    }
```

**Ок.** WONTFIX.

**Но:** `glfwInit` может провалиться **после** успешного создания окна (в `glfwCreateWindow`). Тогда `glfwTerminate` вызывается, но `myWindow` = nullptr. Ок.

---

---

### CR-718. 🔴 `GlfwWindow.cpp` — колбэки через `std::function` — аллокация при каждом Set

**Где:** `GlfwWindow.cpp:76-79`

```
void GlfwWindow::SetMouseButtonCallback(MouseButtonCallback cb) { myMouseButtonCb = std::move(cb); }
```

`std::move` — хорошо. **Но:** `MouseButtonCallback = std::function<void(int,int,int)>` — при **создании** лямбды в `Application.cpp:180` (10 строк closure) — **heap-аллокация**.

**Это происходит один раз** в конструкторе Application. **Не hot-path.** WONTFIX.

---

---

### CR-720. 🟠 `GlfwWindow.cpp` — `ToggleFullscreen` не проверяет `mySavedW != 0`

**Где:** `GlfwWindow.cpp:99-119`

Если пользователь **сразу** нажал F11 в невидимом окне — `glfwGetWindowSize` вернёт 0. При выходе из fullscreen — `glfwSetWindowMonitor(..., mySavedW=0, mySavedH=0, ...)` — **невалидный размер**.

**Фикс:** `if (mySavedW <= 0 || mySavedH <= 0) return;`.

**Оценка:** 3 минуты.

---

---

### CR-721. 🟠 `GlfwWindow.cpp` — `glfwSetErrorCallback` не установлен

**Где:** весь файл

GLFW имеет `glfwSetErrorCallback` — глобальный. **Не установлен.** При ошибках (не найдено OpenGL 3.3, драйвер сломан) — **никакой диагностики**.

**Фикс:** установить в `GlfwWindow::GlfwWindow`:
```
glfwSetErrorCallback([](int code, const char* desc) {
    std::cerr << "GLFW error " << code << ": " << desc << "\n";
});
```

**Оценка:** 5 минут.

---

---

### CR-722. 🟠 `GlfwWindow.h` — `ToggleFullscreen`/`Maximize` — методы без описания

**WONTFIX.**

---

---

### CR-723. 🟠 `GlfwWindow.cpp` — анонимный namespace `Self` — только для 4 колбэков

**Где:** `GlfwWindow.cpp:6-10`

**Функция `Self`** возвращает `GlfwWindow*` через `glfwGetWindowUserPointer`. Используется 4 раза.

**Ок**, паттерн стандартный.

**WONTFIX.**

---

---

### CR-724. 🟠 `GlfwWindow` — `myIsFullscreen` — bool вместо enum

**Где:** `GlfwWindow.h:36`

```
bool myIsFullscreen = false;
```

**Ок** для двух состояний.

**WONTFIX.**

---

---

### CR-725. 🟠 `GlfwWindow.cpp` — `glfwSetWindowUserPointer` вызывается только после `MakeContextCurrent`

**Где:** `GlfwWindow.cpp:37`

**Порядок:**
```
glfwMakeContextCurrent(myWindow);
glfwSwapInterval(1);
glfwSetWindowUserPointer(myWindow, this);
glfwSetMouseButtonCallback(...);   // ← уже после SetWindowUserPointer
```

**Ок.** WONTFIX.

---

---

### CR-726. 🟠 `GlfwWindow` — `#include <iostream>` в заголовке? Нет. Только в .cpp

**WONTFIX.**

---

---

### CR-727. 🟠 `GlfwWindow.cpp` — `glfwWindowHint(GLFW_OPENGL_PROFILE, ...)` — закомментировано

**Связано с CR-714.** Комментарий врёт.

---

## 📊 Итог platform (Волна 16)

**Модулей platform:** 2 (FileDialog, GlfwWindow)
**Найдено записей:** 22 (CR-706..CR-727)

**Критические:**
- **CR-706** — FileDialog только Windows (блокирует кроссплатформенность).
- **CR-714** — Compatibility Profile не установлен (потенциальный краш macOS).
- **CR-715** — `ShouldClose()` UB при null.
- **CR-716** — `~GlfwWindow` убивает GLFW для всех.
- **CR-707** — дубль Utf8-конвертеров.
- **CR-710** — UB в default-параметре filter.

---

---

## 🔴 P0 — platform (Волна 16, часть 3: подтверждение проблем с толстыми линиями)

---

### CR-728. 🔴 Толстые линии (`glLineWidth > 1`) НЕ РАБОТАЮТ — связано с CR-714

**Где:** `GlfwWindow.cpp:19-22`, `OcctView.cpp:44, 165-166, 285, 313` и др.
**Статус:** OPEN (подтверждено MikeVerx, 2026-09-27)
**Категория:** функциональный баг рендеринга

**Что не работает:**
- В эскизе (сетка, оси X/Y): `Prs3d_LineAspect(color, Aspect_TOL_SOLID, 4.0)` — задаём ширину 4, рисуется 1.
- В предпросмотре: `SetWidth(10.0)` — рисуется 1.
- В линиях эскиза: `width = 6.0/7.0/12.0` — рисуется 1.

**Причина:**
- **OpenGL Core Profile** (который даёт GLFW для 3.3+) **deprecated** `glLineWidth > 1`.
- На macOS — **не поддерживается вообще**.
- На Windows — зависит от драйвера.
- OCCT `Prs3d_LineAspect::SetWidth` использует `glLineWidth` → тихо игнорируется.

**Связано:**
- **CR-714** — Compatibility Profile не установлен.
- `OcctView.cpp:44` — `ChangeOptions().contextCompatible = true` — заявка, которая не соответствует реальному контексту.

**Что подтверждает MikeVerx:**
> «Толстые линии и так не получилось заставить работать. Там пока тонкая пакость. Надо будет фиксить. Может даже со всеми багами.»

**Три варианта фикса:**

**A. Установить Compatibility Profile явно (Windows/Linux):**
```
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPATIBILITY_PROFILE);
```
- **Плюсы:** 5 минут, `glLineWidth` заработает.
- **Минусы:** не работает на macOS; deprecated в OpenGL 3.2+.

**B. Собственный шейдер для толстых линий:**
- Рисуем линию как **quad (2 треугольника)** через geometry shader или instanced draw.
- `AIS_Line` в OCCT **не подходит**, но для **эскизной сетки** можно рисовать через `ImDrawList` (уже толстые, антиалиасятся).
- **Плюсы:** работает везде, включая macOS/Vulkan/Metal.
- **Минусы:** 2-3 дня работы, нужно переписать рендер линий в `OcctView`.

**C. Использовать ImGui drawList для линий эскиза (вместо OCCT):**
- `DisplaySketchLine` уже знает начало/конец.
- Рисовать через `ImDrawList::AddLine` в координатах экрана.
- **Плюсы:** работает везде, антиалиасинг, просто.
- **Минусы:** нужно самому проецировать 3D→2D; конфликт с occlusion.
- **Оценка:** 4-6 часов.

**Рекомендую:**
- **Сейчас (быстро):** A — для Windows/Linux.
- **В Фазе 2 (правильно):** B или C — для полной кроссплатформенности.

**Оценка:**
- A — 10 минут.
- B — 3 дня.
- C — 4-6 часов.

**Занести как отдельный баг:** `BUG-A6 — толстые линии не работают`.

---

## 🔴 P0 — app (Волна 16, часть 4: Localization.h)

---

### CR-751. 🟡 `BreezeApplication` — стиль `Instance()` — Qt-style singleton

**WONGFIX.**

---

## 🎯 ФИНАЛ АУДИТА

**Волна 16 (app + doc) — закрыта.**
**Найдено записей:** 23 (CR-729..CR-751)

---

## 📊 ИТОГИ ПОЛНОГО АУДИТА

**Покрытие:**
- ✅ src/ui/ribbon/ (все файлы)
- ✅ src/ui/qat/ (все файлы)
- ✅ src/ui/status/ (все файлы)
- ✅ src/ui/home/ (все файлы)
- ✅ src/ui/dialogs/ (все 8 диалогов)
- ✅ src/ui/panels/ (все файлы)
- ✅ src/ui/theme/ (все файлы)
- ✅ src/ui/widgets/ (все файлы)
- ✅ src/ui/viewcube/ (все файлы)
- ✅ src/app/Application.{h,cpp}
- ✅ src/app/IconManager.{h,cpp}
- ✅ src/app/Localization.{h,cpp}
- ✅ src/doc/BreezeDocument.{h,cpp}
- ✅ src/doc/BreezeApplication.{h,cpp}
- ✅ src/occt/OcctView.{h,cpp}
- ✅ src/platform/FileDialog.{h,cpp}
- ✅ src/platform/glfw/GlfwWindow.{h,cpp}
- ✅ src/util/Utf8.{h,cpp}
- ✅ src/util/Geometry2D.{h,cpp}
- ✅ src/util/LabelName.{h,cpp}
- ✅ CMakeLists.txt

**Всего файлов:** 75+
**Всего записей:** ~751 (CR-001..CR-751)
**Severity:**
- P0: ~250
- P1: ~180
- P2: ~150
- P3: ~50
- RAM: 13

**Главные категории:**
1. **Локализация (хардкод строк)** — ~200 мест.
2. **Кроссплатформенность** — CR-679 (Utf8), CR-706 (FileDialog), CR-714 (Compatibility Profile), CR-728 (толстые линии).
3. **Слоение (UI знает OCCT)** — ~20 мест.
4. **Хот-пас аллокации** — ~30 мест.
5. **RULES п.20 (static)** — ~10 мест.
6. **RULES п.30 (хардкод цветов)** — ~40 мест.
7. **Функциональные баги** — CR-049/050/051/053 (ViewTab), CR-232 (транзакции OCAF), CR-605 (splitter), CR-611 (debug-вывод), CR-657 (BUG-4.1), CR-694 (duplicate menu).
8. **Layout magic числа** — ~100 мест, 6 разных «высот ribbon».

**Готово к переразбиению.**

---

---

