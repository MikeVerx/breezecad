# BreezeCAD — Архитектура

**Единый источник правды о структуре кода.**
**Дата:** 2026-09-23

---

## 1. Философия

**Код должен быть приятным для чужого программиста.**
Опытный C++-разработчик открывает проект — понимает за 5 минут.

**Следствия:**
- Слои с чёткими границами.
- Никаких монолитов >500 строк.
- Единый стиль (clang-format).
- README, ARCHITECTURE, CONTRIBUTING.
- Тесты для ядра.

---

## 2. Слои

    ┌─────────────────────────────────────────┐
    │  ui/        ImGui, окна, ribbon, темы   │
    ├─────────────────────────────────────────┤
    │  app/       Application, команды,       │
    │             состояния                   │
    ├─────────────────────────────────────────┤
    │  doc/       BreezeDocument,             │
    │             DocumentData, история       │
    ├─────────────────────────────────────────┤
    │  kernel/    IKernel (интерфейс),        │
    │             OcctKernel (реализация)     │
    ├─────────────────────────────────────────┤
    │  platform/  GlfwWindow, FileDialog      │
    └─────────────────────────────────────────┘

**Правило:** слой может зависеть только от слоёв НИЖЕ себя.
UI не знает про OCCT. Ядро не знает про ImGui.

---

## 3. Структура репозитория (целевая)

    src/
      main.cpp
      app/
        Application.cpp/h
        CommandStack.cpp/h
        Localization.cpp/h
        TooltipRegistry.cpp/h
      ui/
        ribbon/
          Ribbon.cpp/h
          RibbonGroup.cpp/h
          RibbonButton.cpp/h
          tabs/
            ModelTab.cpp
            SketchTab.cpp
            AssemblyTab.cpp
            ViewTab.cpp
        panels/
          LeftPanel.cpp/h
          RightPanel.cpp/h
        dialogs/
          Settings.cpp
          About.cpp
        HomeScreen.cpp/h
        ViewCube.cpp/h
      doc/
        BreezeDocument.cpp/h
        DocumentData.cpp/h
        History.cpp/h
        Migration.cpp/h
      kernel/
        IKernel.h
        OcctKernel.cpp/h
        ShapeBuilder.cpp/h
        BooleanOps.cpp/h
      platform/
        glfw/
          GlfwWindow.cpp/h
        FileDialog.cpp/h

**Цель Фазы 1.5:** привести `src/` к этой структуре.

---

## 4. Ядро — IKernel

**Интерфейс абстракции геометрии.**

Зачем: сегодня OCCT, завтра — своё ядро или альтернатива.

    class IKernel {
    public:
        virtual ShapeHandle extrude(Profile, double distance) = 0;
        virtual ShapeHandle revolve(Profile, Axis, double angle) = 0;
        virtual ShapeHandle booleanUnion(ShapeHandle, ShapeHandle) = 0;
        virtual ShapeHandle booleanCut(ShapeHandle, ShapeHandle) = 0;
        virtual ShapeHandle fillet(ShapeHandle, Edges, double radius) = 0;
        // ...
    };

**Правило:** UI и doc работают только с `IKernel*`. Никаких прямых
вызовов OCCT вне `kernel/`.

**ADR-0001** описывает стратегию ядра (гибрид: урезанный OCCT).

---

## 5. Документ — DocumentData

**Три представления одного документа:**

1. **DocumentData** — независимая от ядра структура.
   - history: список OperationRecord.
   - parameters: переменные.
   - materials: материалы.
   - Парсится из .bzc.

2. **Граф операций** — в памяти, при открытии строится из history.
   - Каждая операция имеет входы (parents) и выходы.
   - При изменении параметров — пересчёт потомков.

3. **B-rep тела** — результат применения операций.
   - Кэшируется.
   - Может быть пересчитан из графа.

**Принцип:** история — источник правды. B-rep — кэш.

---

## 6. UI — ImGui

**Три уровня:**

1. **`ui/ribbon/`** — верхнее меню (QAT + 4 вкладки).
   - Каждая вкладка — свой файл.
   - Группы и кнопки — общие helpers.

2. **`ui/panels/`** — панели.
   - Левая (Дерево / Параметры / Переменные).
   - Правая (навигация).

3. **`ui/dialogs/`** — модальные окна.
   - Настройки, О программе.

**Правило:** UI не считает геометрию. Только отображение + команды.

---

## 7. Потоки

**Основной поток:** UI + рендер. 60 FPS.

**Фоновые потоки:**
- **Autosave** — раз в минуту, atomic write.
- **Tessellation** — после изменения модели.
- **Long operations** — Boolean, Fillet на большой геометрии.

**Обмен:** thread-safe queue команд + callback с прогрессом.

**Правило:** тяжёлое никогда не блокирует UI.

---

## 8. Плагины (Фаза 5+)

**Триггер:** ядро стабильно, .bzc работает, 3-5 модулей от нас.

**API:**
- C-ABI (extern "C") для стабильности.
- Lua для скриптов.
- JSON для UI-тем.

**Слоты:**
- Вкладка в ribbon.
- Команда в контекстное меню дерева.
- Свой импорт/экспорт.
- Свои операции через IKernel.

**Что нельзя:**
- Прямой доступ к OCCT.
- Модификация UI вне слотов.
- Перехват событий других плагинов.

**Защита от крашей:**
- Отдельный процесс-менеджер.
- Диалог при краше: скопировать отчёт, отключить плагин, восстановить.

---

## 9. Модульная система

**Что в базе:** деталь, эскиз, сборка, стандартные изделия, STL Viewer.

**Что модульно:** черчение, OCAF-мост, рендер с трассировкой.

**Manifest модуля:**
- name, version, author
- dependencies
- min_gpu_ram_mb, min_ram_mb, min_cores
- entry_point (DLL/SO)

**При установке:** сверяем требования с системой. Не прошло — disabled.

---

## 10. Соглашения по коду

- **Язык:** C++17.
- **Стиль:** clang-format, .clang-format в корне.
- **Именование:**
  - Классы: PascalCase.
  - Функции: camelCase.
  - Приватные поля: `my` + PascalCase (`myWindow`).
  - Константы: kPascalCase (`kMaxUndoSteps`).
- **Заголовки:** `#pragma once`, forward declarations где можно.
- **Ошибки:** исключения в ядре, коды в UI.
- **Логи:** `std::cout` для старта, TRACE для отладки.
- **Тесты:** Catch2 или doctest, только для ядра.

---

## 11. Потоки данных

### Команда пользователя

    UI клик → Application::ExecuteCommand(id) → Command::Execute()
        ↓
    IKernel::operation(...)  → TopoDS_Shape
        ↓
    BreezeDocument::AddFeature(shape)
        ↓
    History.append(OperationRecord)
        ↓
    UI обновляется (re-render)

### Загрузка файла

    .bzc → manifest → migration? → history → IKernel → B-rep
                                                       ↓
                                                  UI + tessellation

### Сохранение файла

    History + Parameters + Materials → DocumentData → .bzc (ZIP)

---

## 12. Рефакторинг (Фаза 1.5)

**Что делаем:**
1. Разбить `Application.cpp` (4700 строк) на модули.
2. Ввести слои `platform / kernel / doc / app / ui`.
3. Убрать циклические зависимости.
4. clang-format единый.
5. README + CONTRIBUTING.
6. Тесты для ядра (базовые).

**Цель:** новый разработчик понимает проект за 5 минут.

---

## 13. Правила для нового чата

1. Не нарушать слои (UI -> app -> doc -> kernel -> platform).
2. Не добавлять прямой OCCT в UI/doc.
3. Файлы >500 строк — разбивать.
4. Любое архитектурное решение — ADR.
5. Любая новая зависимость — обоснование.
