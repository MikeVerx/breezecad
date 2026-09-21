# BreezeCAD — Technical Debt

Известные баги, ограничения, незакрытые вопросы. Каждый пункт — с историей
попыток и текущим статусом, чтобы новые чаты не повторяли уже пройденное.

---

## TECH-DEBT-001: Белый экран при запуске без gdb

**Статус:** OPEN
**Приоритет:** HIGH
**Дата заведения:** 2026-09-21

### Симптом

Приложение при запуске показывает белое окно. Логи доходят до
`InitCommands: 15 commands loaded` и на этом останавливаются.
GUI не рисуется.

При запуске через `gdb --batch -ex run -ex args ./breezecad.exe` —
программа стартует нормально (не всегда, но часто).

### Что уже проверено и НЕ помогло

1. **Полная пересборка** (`rm -rf build && cmake -B build -G Ninja && ninja`).
   Иногда помогает, иногда нет. Нестабильно.
2. **`ninja -t clean && ninja`.** Аналогично — иногда помогает.
3. **Отключение ImGui рендера** (`ImGui::Render` + `ImGui_ImplOpenGL3_RenderDrawData`
   закомментированы). Не помогло — белый экран остался.
4. **`taskkill //F` перед запуском** (гипотеза: висящий GL-контекст).
   Убрано из `run.sh` — гипотеза не подтвердилась.
5. **Явное `glfwMakeContextCurrent` перед ImGui NewFrame и RenderDrawData.**
   Добавлено в код — не помогло.
6. **`glViewport(0, 0, fbW, fbH)` перед RenderDrawData.** Не помогло.
7. **Манифест** — стандартный Windows-манифест с `supportedOS` для Win7-Win11,
   `asInvoker`. Ничего подозрительного.
8. **GLFW 3.5.1** — актуальная версия.
9. **Compatibility Profile** (`contextCompatible = true`) — уже включено
   в `OcctView::Init`.
10. **Debug-trace** по точкам: конструктор `Application`, вход в `Run()`,
    главный цикл, `PollEvents`, `NewFrame`, `OcctView::Redraw`,
    `RenderDrawData`, `SwapBuffers` — все проходят. Проблема дальше, в
    самой отрисовке GL.

### Что установлено точно

- Логи `std::cout` идентичны в рабочем и нерабочем режиме.
- Debug-точки идут **до** `SwapBuffers` и `After SwapBuffers` в обоих режимах.
- Приложение не падает — процесс жив, крутит цикл.
- Проблема не в коде логики: она в **GL-состоянии** или **порядке инициализации**.
- `taskkill /F` не связан (проверено).

### Возможные направления для поиска

1. **Проверить, какой GL-контекст активен** в момент `RenderDrawData`.
   Вывести `glfwGetCurrentContext()` и сверить с `myWindow->GetNativeWindow()`.
2. **Проверить `Aspect_NeutralWindow` + `SetWindow`** в `OcctView::Init` —
   OCCT создаёт свой контекст поверх GLFW. Возможно, между ними race.
3. **`glfwSwapInterval(1)`** — VSync. Попробовать без него (0) — иногда
   на слабых драйверах VSync блокирует первый кадр.
4. **Попробовать `glFinish()`** после `SwapBuffers` — вдруг асинхронный
   swap не успевает.
5. **Тайминги инициализации.** Возможно, `OcctView::Init` вызывается
   слишком рано после `glfwCreateWindow` — драйвер не готов. Попробовать
   `glfwWaitEvents()` + небольшая задержка.
6. **Двойная инициализация GLFW.** Проверить, что `glfwInit()` вызывается
   ровно один раз (в `GlfwWindow` конструкторе).
7. **Проверить размер framebuffer.** Возможно, `fbW=0, fbH=0` в момент
   первого `FitAll` / `Redraw`, и рендер уходит в никуда.
8. **Проверить `myOcctView->Init` на возврат `false`** — сейчас код делает
   `myOcctView.reset()` и `return`, что оставляет Application в
   нерабочем состоянии без сообщения пользователю.

### Обходной путь (текущее решение)

`run.sh` использует **gdb --batch** для запуска. Работает нестабильно,
но чаще всего запускает приложение.
**Это костыль, требует исправления.**

---

## TECH-DEBT-002: Кириллица в пути SaveAs → кракозябры

**Статус:** OPEN
**Приоритет:** MEDIUM

### Симптом

Сохраняя документ в `C:\Users\Мишаня\Downloads\123.cbf`, OCAF выводит:
Файл сохраняется, но с битым путём в логе.

### Гипотеза

Проблема в `Utf8ToExt` / `TCollection_ExtendedString` при конвертации
кириллицы. Используется `(wchar_t*)` как если бы `wchar_t` был 16-bit,
но на MinGW это может быть 32-bit (зависит от настроек).

### Что попробовать

- Проверить `sizeof(wchar_t)` в MinGW.
- Использовать `TCollection_ExtendedString(const char*, Standard_Boolean isMultiByte)`
  напрямую вместо ручной конвертации.
- Или передавать `std::filesystem::path::wstring()` — тоже аккуратно.

---

## TECH-DEBT-003: run.sh ищет ассеты по CWD

**Статус:** FIXED (2026-09-21)
**Коммит:** (в run.sh исправлено)

`run.sh` теперь `cd "$(dirname "$0")/build"`, потом `cd bin`.
Ассеты находятся корректно.

---

## TECH-DEBT-004: IconManager не находит 5 иконок

**Статус:** OPEN
**Приоритет:** LOW

### Симптом


Иконки используются в UI, но их нет в Lucide (или другие имена).

### Что делать

Найти аналоги в Lucide и добавить в `kIconAliases` в `IconManager.cpp`:
- `align-justify` → `align-justify` (есть ли?) → возможно `align-horizontal-justify-center`
- `waves` → `waves` (проверить в assets)
- `distance` → `move-3d` или `ruler`
- `alert-triangle` → `triangle-alert`
- `alert-circle` → `circle-alert`

---

## TECH-DEBT-005: Warnings OCCT про Standard_True/False/Real

**Статус:** OPEN
**Приоритет:** LOW

### Симптом


### Где встречается

- `src/doc/BreezeDocument.cpp` — Undo/Redo, стандартный шаблон
- `src/occt/OcctView.cpp` — DisplaySketchLine
- `src/app/Application.cpp` — OnExportSTL, EnterSketch

### Что делать

Заменить:
- `Standard_True` → `true`
- `Standard_False` → `false`
- `Standard_Real` → `double`

Через Python-скрипт, сохраняя CRLF. Не срочно.

---

## TECH-DEBT-006: Application.cpp — god-object

**Статус:** OPEN
**Приоритет:** MEDIUM


### Где встречается

- `src/doc/BreezeDocument.cpp` — Undo/Redo, стандартный шаблон
- `src/occt/OcctView.cpp` — DisplaySketchLine
- `src/app/Application.cpp` — OnExportSTL, EnterSketch

### Что делать

Заменить:
- `Standard_True` → `true`
- `Standard_False` → `false`
- `Standard_Real` → `double`

Через Python-скрипт, сохраняя CRLF. Не срочно.

---

## TECH-DEBT-007: Двойное кодирование UTF-8 в guillemets

**Статус:** FIXED (2026-09-21)
**Причина:** мой Python-патч дважды закодировал символы `«` и `»`.

Было: `c3 82 c2 ab` (UTF-8 от `Â«`)
Стало: `c2 ab` (правильный UTF-8 `«`)

Исправлено в `Application.cpp`, коммит — см. `git log`.

**Урок:** в Python-патчах использовать текстовый режим
(`encoding='utf-8', newline=''`), не ручные `b'\xc2\xab'` в байтовых строках.

---

## Заметки о workflow

### Что работает хорошо

- **Python heredoc** (`python << 'PYEOF'`) для правок — сохраняет CRLF,
  не ломает UTF-8.
- **`grep -a`** для UTF-8 файлов.
- **`objdump -p | tr -d '\0' | grep -aE 'DLL Name:' | awk '{print $3}'`**
  для получения зависимостей DLL.
- **`LC_ALL=C`** для предотвращения локализованного вывода objdump.

### Что не работает / опасно

- **`sed -i`** — ломает CRLF (весь файл переписывается LF-переводами).
- **`grep` без `-a`** — файл считается бинарным, поиск молчит.
- **Кириллица в `python -c "..."`** — синтаксические ошибки.
- **`taskkill //F`** — не связано с текущим багом, но вообще грубо.

### Правила для следующего чата

1. Правки только через консольные команды. Не «открой файл в редакторе».
2. После каждой правки — команда проверки (grep/diff).
3. После сборки — команда запуска для теста.
4. Не предлагать ребут как решение.
5. Если что-то сломалось после изменения — откатывать целиком, не «докручивать».
