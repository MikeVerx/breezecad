# BreezeCAD — памятка для Claude

Бесплатная кроссплатформенная САПР: C++20, OpenCASCADE, Dear ImGui. Отвечать по-русски.

## Нельзя нарушать
- В `next` и `main` сливает только автор (MikeVerx). Работа — в своей ветке, PR в `next`.
- Модули с основой не сливаются никогда. `modules/mcp` не трогать.
- Офлайн, без телеметрии и сетевых запросов.
- Видимый текст — только `Tr("…")` / `Trf("… {}", x)`; цвета — только токены темы; переменные не называть `far`/`near`.
- Без автора не трогать: форматы файлов (`document/BzcFile*`, `JsonIO*`, `assembly/AssemblyFile*`), решатель сборки, `kernel/AssemblyModel*`, `AutoDrop*`, `ComponentPattern*`, `CMakeLists.txt`, `.github/`, `third_party/`.
- Новые интерфейсы — сначала макет, в код после согласия автора.

## Где подробности
- **База знаний проекта** — приватный репозиторий `MikeVerx/breezecad-vault`. Если он подключён к сессии — начинать с его `00-Index/Home.md` и `Status.md`, а документы ниже открывать только за деталями.
- Без неё: `docs/dev/HANDOFF.md` (разделы по теме, не целиком), `docs/ARCHITECTURE.md`, `docs/dev/FIX-BRANCH.md` (правила кода и коммитов).
- `docs/dev/WORKLOG.md` и большие исходники (`SketchTools.cpp`, `Model.cpp`, `Application.cpp`) — только `grep -n` и чтение кусками. `docs/legacy/` не читать.

## Сборка и проверка
`bash scripts/dev/cloud-setup.sh` в облачной песочнице, затем `cmake --preset linux-debug`, сборка, `./build/linux-debug/bin/bz_tests`. Windows и macOS — только CI.

Коммиты по-русски: `тип(область): что` + What / Why / Where / Tested, одна логическая часть на коммит.
