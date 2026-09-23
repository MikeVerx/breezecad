# BreezeCAD

Кросс-платформенный САПР. Лёгкий Inventor / КОМПАС-3D для слабого железа
(4 ГБ ОЗУ, iGPU), ARM-планшетов, Mac.

Целевой вес: 200-500 МБ (<300 желательно).
Целевой старт: <1 сек на среднем железе.

---

## Философия

**Уважение ко времени пользователя.** Автосохранения, восстановление
после крашей, никакой телеметрии.

**Языки.** Сейчас — русский (для скорости разработки). Языковые пакеты
расширим, когда закроем MVP. Архитектура локализации уже готова
(Localization.cpp), новый язык = дописать словарь.

**Код — приятный для чужого программиста.** Опытный C++-разработчик
должен понять проект за 5 минут и не поморщиться.

**Дизайн — Inventor + КОМПАС.** Структура от Inventor, частично от
КОМПАС. Взаимодействие клава-мышь — от КОМПАС (удобнее, чем в Inventor).

---

## Что НЕ делаем

FEA/CFD, CAM, BIM, облако, обратный инжиниринг, generative design,
синхронное моделирование, IGES, импорт .ipt/.sldprt/.m3d.

---

## Быстрый старт

Требования:
- Windows 11 (текущая цель) / Linux / macOS.
- MSYS2 MINGW64 (Windows) или g++ 16+, cmake 3.16+, ninja.
- OpenCASCADE 7.7+ в ~/occt/install/.
- lunasvg 3.5+ в ~/lunasvg/install/.

Сборка:

    cd ~/BreezeCAD/build
    cmake ..
    ninja

Запуск:

    ~/BreezeCAD/run.sh

Проверка зависимостей DLL:

    LC_ALL=C objdump -p build/bin/breezecad.exe | tr -d '\0' | grep -aE 'DLL Name:' | awk '{print $3}'

---

## Стек

| Компонент | Что |
|---|---|
| Язык | C++17 |
| Сборка | CMake + Ninja + MSYS2/MinGW64 |
| Ядро геометрии | OpenCASCADE (урезанная, ADR-0001) |
| Документ | OCAF (временно; цель — свой .bzc) |
| UI | Dear ImGui + GLFW 3.5.1 |
| Рендер | OpenGL 3.3 через OCCT V3d |
| Иконки | Lucide + свои CAD (SVG -> lunasvg) |
| Шрифты | Inter, Roboto, Font Awesome Solid |
| Локализация | RU/EN, свой словарь |

---

## Структура

    BreezeCAD/
      assets/          иконки, шрифты
      build/           out-of-source сборка (git-ignored)
      docs/            проектная документация
      external/        ImGui, stb (git-ignored)
      resources/       app.ico, app.rc
      src/             исходный код
      tools/           svg2ico и утилиты
      CMakeLists.txt
      run.sh
      README.md

Целевая структура src/ (Фаза 1.5):

    src/
      main.cpp
      app/         Application, CommandStack, Localization
      ui/          ribbon, panels, dialogs, HomeScreen, ViewCube
      doc/         BreezeDocument, DocumentData, History, Migration
      kernel/      IKernel, OcctKernel, ShapeBuilder, BooleanOps
      platform/    GlfwWindow, FileDialog

---

## Документация

Точки входа (читать первыми):
- docs/HANDOFF.md — инструкция для нового чата.
- docs/RULES.md — 38 правил работы.
- docs/PROJECT_STATE.md — текущее состояние.

Канон:
- docs/CANON_FEATURES.md — что должно быть.
- docs/BUTTONS_INVENTORY.md — перечень кнопок.
- docs/UI_LAYOUT.md — детальный дизайн UI.
- docs/FILE_FORMAT.md — формат .bzc.
- docs/ARCHITECTURE.md — слои, ядро, UI.

Журналы:
- docs/ROADMAP.md — план по фазам 0-9.
- docs/TECH_DEBT.md — открытые баги.
- docs/UI_ISSUES.md — визуальные баги.
- docs/METHODS.md — что работает, что нет.
- docs/CHANGELOG_WORK.md — история по фазам.

Для типовых задач:
- docs/PROMPTS/ — шаблоны для сессий.
- docs/ADR/ — Architecture Decision Records.

---

## Статус

- Фаза 0 — Проектирование UI: закрыта.
- Фаза 1 — Скелет UI: закрыта.
- Фаза 1.5 — Рефакторинг: следующая.
- Фаза 2 — Функционал детали: запланирована.

Полный план — docs/ROADMAP.md.

---

## Лицензия

**Mozilla Public License 2.0** (MPL-2.0).

BreezeCAD — свободное ПО. Вы можете использовать, изменять и распространять
его на условиях MPL 2.0. Полный текст — в файле [LICENSE](LICENSE).

### Что это значит

- Открытое ядро: изменения в файлах BreezeCAD возвращаются в проект.
- Гибкость: можно строить закрытые модули поверх BreezeCAD.
- Патентная защита (раздел 2.1 MPL 2.0).
- Совместимость с GPL 2.0 / 3.0 / AGPL 3.0.

Проект развивается в духе Blender и FreeCAD: открытый код, сообщество,
финансирование через гранты и донаты.

---

## Авторы

- **MikeVerx** — автор проекта, идея, архитектура, весь UI-дизайн.
- **AI-ассистент проекта** — соавтор документации, рефакторинга и части кода.
