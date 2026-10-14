<p align="center"><img src="docs/images/banner.png" alt="BreezeCAD — свободный параметрический САПР" width="100%"></p>

<p align="center">
  <b>Скачать:</b>
  <a href="https://github.com/MikeVerx/breezecad/releases/download/next-build/BreezeCAD-windows.zip"><b>Windows</b></a> ·
  <a href="https://github.com/MikeVerx/breezecad/releases/download/next-build/BreezeCAD-macos.zip"><b>macOS</b></a> ·
  <a href="https://github.com/MikeVerx/breezecad/releases/download/next-build/BreezeCAD-linux.tar.gz"><b>Linux</b></a> ·
  <a href="https://github.com/MikeVerx/breezecad/releases/tag/modules"><b>Модули</b></a> ·
  <a href="#сборка-из-исходников">Сборка из исходников</a> ·
  <a href="docs/ARCHITECTURE.md">Архитектура</a> ·
  <a href="README.md">English</a>
</p>

**BreezeCAD** — свободный параметрический САПР без интернета: для деталей,
которые печатают, фрезеруют или заказывают. Эскиз, связи, выдавливание,
отверстия и резьба — в привычной ленте в духе Inventor. Запускается за
секунду и работает на ноутбуке с 4 ГБ памяти и встроенной графикой.

<p align="center"><img src="docs/images/model-light.png" alt="Фланец с массивом отверстий под винты и резьбой M30" width="100%"></p>

## Зачем

- **Офлайн всегда.** Без аккаунта, облака и телеметрии. Файлы — ваши.
- **Лёгкий.** Небольшая загрузка, мгновенный запуск на скромном железе.
- **Привычный.** Лента, дерево построения и панели — как ждут пользователи
  Inventor и КОМПАС-3D; наборы клавиш и мыши под оба.
- **Открытый.** MPL-2.0: читайте код, меняйте, делайте свои модули.

## Что умеет сейчас

| Эскиз | Модель |
| --- | --- |
| Отрезки, дуги, окружности, эллипсы, сплайны, прямоугольники, пазы, многоугольники, текст | Выдавливание и вращение (добавить, вырезать, пересечь), стрелки-манипуляторы |
| Зависимости и размеры, живой решатель, степени свободы | Отверстия: простые, с цековкой, с зенковкой |
| Обрезка, удлинение, разбиение, скругление, эквидистанта, перенос, поворот, масштаб, зеркало, массивы | Сопряжение, фаска, лофт, сдвиг, пружина |
| Проецирование рёбер тела в эскиз | **Моделированная метрическая резьба** на валу и в отверстии |
| | Рабочие плоскости и оси, массивы, зеркало |
| | Разрез вида, измерения, МЦХ, **поиск коллизий** |
| | Экспорт STEP, STL, 3MF · свой формат `.bzc` |

<table>
<tr>
<td width="50%"><img src="docs/images/sketch.png" alt="Эскиз с размерами"><br><sub>Эскиз с касаниями и управляющими размерами</sub></td>
<td width="50%"><img src="docs/images/thread.png" alt="Панель резьбы"><br><sub>Резьба: щелчок по валу или отверстию — размер определяется сам (M30×3.5)</sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/images/model-dark.png" alt="Тёмная тема"><br><sub>Светлая и тёмная тема, масштаб интерфейса под любой экран</sub></td>
<td width="50%" valign="top">

**Состояние.** Ранняя версия: моделирование деталей уже работает, сборок и
чертежей пока нет. Шероховатости будут —
[сообщайте о них](https://github.com/MikeVerx/breezecad/issues).

**Языки.** Интерфейс сейчас на русском; переводы — обычные JSON-каталоги,
см. [`translations/`](translations/README.md).

</td>
</tr>
</table>

## Скачать

Свежая сборка ветки `next`: обновляется после каждого изменения, прошедшего
тесты на всех трёх системах ([все файлы](https://github.com/MikeVerx/breezecad/releases/tag/next-build)).
Распаковать и запустить — ничего устанавливать не нужно.

| Система | Скачать | Состояние |
| --- | --- | --- |
| Windows 10/11, 64-бит | [BreezeCAD-windows.zip](https://github.com/MikeVerx/breezecad/releases/download/next-build/BreezeCAD-windows.zip) | основная, идёт бета-тест |
| macOS 14+ на процессорах Apple (M1 и новее) | [BreezeCAD-macos.zip](https://github.com/MikeVerx/breezecad/releases/download/next-build/BreezeCAD-macos.zip) | новая: собирается и проходит тесты в CI, первые запуски на живых маках — сейчас |
| Linux x86_64 (Ubuntu 20.04+, Debian 10+, Fedora и другие не старше 2019 года) | [BreezeCAD-linux.tar.gz](https://github.com/MikeVerx/breezecad/releases/download/next-build/BreezeCAD-linux.tar.gz) | собирается, проходит тесты и запускается в CI |

**macOS, первый запуск:** приложение пока без платной подписи Apple, и
macOS один раз его остановит. Правой кнопкой по `BreezeCAD.app` → **Открыть** →
**Открыть**; на macOS 15 и новее: двойной щелчок, затем **Системные настройки →
Конфиденциальность и безопасность → Всё равно открыть**.

Бета-тестерам — сборки `BreezeCAD-beta-*` того же выпуска: они записывают всё
нужное для отчётов об ошибках (только на вашем компьютере), внутри — инструкция.

Тачпад работает на всех трёх системах: два пальца — сдвиг (с Shift — вращение),
щипок — масштаб; на маке ещё поворот двумя пальцами, «умный зум» и сильное
нажатие. Всё — в **Настройки → Тачпад**.

Последний пронумерованный выпуск с описанием изменений — на странице
[«Выпуски»](https://github.com/MikeVerx/breezecad/releases/latest).

### Модули

Модули добавляют программе новые возможности и скачиваются отдельно: только
те, что нужны. Все модули, что они делают и файлы под каждую систему — на
странице **[«Модули BreezeCAD»](https://github.com/MikeVerx/breezecad/releases/tag/modules)**. Ставятся в **Настройки → Модули →
«Установить модуль…»** или перетаскиванием файла `.bzmod` в окно программы.

| Модуль | Что делает |
| --- | --- |
| [MCP-сервер для Claude](modules/mcp/README.md) | Claude строит в открытом BreezeCAD: эскизы, операции, сборки, картинка модели — 65 инструментов |

## Сборка из исходников

Нужны компилятор C++20, CMake ≥ 3.24, Ninja, OpenCASCADE (7.6+ или 8), GLFW 3
и OpenGL 3.3.

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build libocct-*-dev libglfw3-dev libgl1-mesa-dev libxi-dev
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug          # тесты
./build/linux-debug/bin/breezecad
```

```sh
# macOS (процессоры Apple), Homebrew
brew install cmake ninja glfw opencascade
cmake --preset macos -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build --preset macos
ctest --preset macos
./build/macos/bin/breezecad
```

Windows — MSYS2 / MinGW64; упаковка для всех трёх систем — `scripts/package-*.sh`.
Точные шаги — в [`.github/workflows/build.yml`](.github/workflows/build.yml).

## Участие

Начните с [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — модули, поток данных
и пошаговые рецепты: как добавить инструмент эскиза, операцию или настройку.
Затем [`CONTRIBUTING.md`](CONTRIBUTING.md) — правила и формат коммитов.
Ошибки и идеи — в [issues](https://github.com/MikeVerx/breezecad/issues).

## Лицензия

[Mozilla Public License 2.0](LICENSE). Изменения файлов BreezeCAD возвращаются
в проект; свои модули поверх него можно держать закрытыми.

Автор — **MikeVerx**. Геометрия — [Open CASCADE Technology](https://dev.opencascade.org),
интерфейс — [Dear ImGui](https://github.com/ocornut/imgui).

<sub>Autodesk Inventor — товарный знак Autodesk, Inc.; КОМПАС-3D — товарный знак АСКОН. Они упомянуты только для описания привычных приёмов работы; BreezeCAD — независимый проект, не связан с ними и не одобрен ими, не использует их код, иконки и файлы.</sub>
