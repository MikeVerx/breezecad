<p align="center"><img src="docs/images/banner.png" alt="BreezeCAD — свободный параметрический САПР" width="100%"></p>

<p align="center">
  <a href="https://github.com/MikeVerx/breezecad/releases/download/next-build/BreezeCAD-windows.zip"><b>Скачать для Windows</b></a> ·
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

## Сборка из исходников

Нужны компилятор C++20, CMake ≥ 3.24, Ninja, OpenCASCADE (7.6+ или 8), GLFW 3
и OpenGL 3.3.

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build libocct-*-dev libglfw3-dev libgl1-mesa-dev
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug          # тесты
./build/linux-debug/bin/breezecad
```

Windows — MSYS2 / MinGW64, точные шаги в
[`.github/workflows/build.yml`](.github/workflows/build.yml).

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
