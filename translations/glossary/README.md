# Глоссарий переводчика / Translator's glossary

BreezeCAD — параметрическая 3D-САПР для инженеров и мейкеров (3D-печать).
Используйте устоявшуюся терминологию САПР своего языка — так, как её называют
локализованные Autodesk Inventor, SolidWorks, Fusion 360, FreeCAD.

Use the established CAD terminology of your language — the terms localized
Autodesk Inventor / SolidWorks / Fusion 360 / FreeCAD use. Keep one term per
concept across the whole catalog.

## Rules

- `{}`, `{:.1f}`, `{:.3g}` — placeholders: same ones, same order (reorder the sentence, not the holes).
- `\n` — line break (ribbon captions are two short lines: keep them short, ≤ ~12 characters per line).
- Leading/trailing spaces and punctuation like ` · `, `—`, `✓`, `→`, `«»` around holes: keep the spacing; use your language's quotes instead of «» if natural.
- Short UI labels stay short. Imperatives in status hints ("Щёлкните грань") → the form your OS uses in hints (en: "Click a face").
- Key names stay: Enter, Esc, Shift, Ctrl, Alt, Tab, Space→local name ok, ЛКМ = LMB (left mouse button), ПКМ = RMB, СКМ = MMB (use your language's usual abbreviation or "left click").
- ГОСТ → GOST in Latin scripts (ГОСТ in Cyrillic ones). ЕСКД → ESKD. КОМПАС-3D → KOMPAS-3D. Inventor, SolidWorks, Blender, STL, 3MF, STEP, DXF, MCP, BreezeCAD, GitHub stay as they are.
- File extensions (.bzc, .bza, .bzp, .bzmod, .bzlib), JSON keys, code in backticks: unchanged.
- Formal "you" where the language has it (de: Sie, fr: vous, es: usted is fine but neutral imperatives preferred, pl/cs: neutral imperative, uk/be: ви).
- When a short string is ambiguous, find it in the source: `grep -rn "Паз" src` shows where it is used.

## Terms (RU → EN reference)

| RU | EN | note |
|---|---|---|
| Деталь | Part | |
| Сборка | Assembly | |
| Компонент (сборки) | Component | |
| Проект (.bzp) | Project | assembly with all its parts in one file |
| Модуль (.bzmod) | Module / add-in | plugin |
| Библиотека (.bzlib) | Library | |
| Эскиз | Sketch | |
| Эскиз на грани | Sketch on face | |
| Тело | Body | solid body |
| Грань | Face | |
| Ребро | Edge | (not "rib") |
| Ребро жёсткости | Rib | |
| Вершина | Vertex | |
| Контур | Profile / loop | closed sketch region |
| Отрезок | Line (segment) | sketch line |
| Дуга, Окружность | Arc, Circle | |
| Сплайн | Spline | |
| Прямоугольник, Многоугольник, Паз | Rectangle, Polygon, Slot | |
| Вспомогательная геометрия | Construction geometry | |
| Проецирование / спроецировать | Project geometry | projected (sketch) geometry |
| Обрезка / Обрезать | Trim | |
| Удлинение / Удлинить | Extend | |
| Смещение (кривой) | Offset | |
| Сдвиг | Move / Shift (context) | |
| Зеркало / Зеркальное отражение | Mirror | |
| Массив (круговой, прямоугольный, по траектории) | Pattern (circular, rectangular, along path) | |
| Зависимость (в эскизе) | Constraint | |
| Совпадение, Касание, Параллельность, Перпендикулярность, Горизонтальность, Вертикальность, Равенство, Симметрия, Фиксация, Концентричность | Coincident, Tangent, Parallel, Perpendicular, Horizontal, Vertical, Equal, Symmetric, Fix, Concentric | |
| Размер | Dimension | |
| Выдавливание | Extrude | |
| Вращение | Revolve | |
| По траектории | Sweep | |
| По сечениям | Loft | |
| Вырез | Cut | |
| Отверстие | Hole | |
| Резьба | Thread | |
| Фаска | Chamfer | |
| Скругление | Fillet | |
| Оболочка | Shell | |
| Уклон | Draft | |
| Пружина | Coil / spring | |
| Сечение (вид) | Section view | |
| Рабочая плоскость / ось / точка | Work plane / axis / point | |
| Начало координат | Origin | |
| Сопряжение (сборки) | Mate / joint | Inventor: Constraint; use "Mate" style term |
| Закрепить (компонент) | Ground | |
| Вставить компонент | Place component | |
| Редактировать на месте | Edit in place | |
| Листовой металл | Sheet metal | |
| Отбортовка / Полка | Flange | |
| Сгиб | Bend | |
| Развёртка | Flat pattern | |
| Обечайка | Contour roll / rolled shell | |
| Лента (интерфейса) | Ribbon | |
| Дерево (построения) | Browser / model tree | |
| Спецификация | Bill of materials / parts list | ГОСТ «спецификация» |
| Обозначение (по ГОСТ) | Designation / part number | |
| Позиция (в спецификации) | Item number | |
| Материал | Material | |
| Масса | Mass | |
| Горячее меню | Marking menu (hot menu) | |
| Привязка | Snap | |
| Измерение | Measure | |
| Повреждён(о) | Corrupted / damaged | |
| Автосохранение | Autosave | |
| Пакет (языковой) | (language) pack | |
| Стандартные изделия / крепёж | Standard parts / fasteners | |
| Винт, Болт, Гайка, Шайба | Screw, Bolt, Nut, Washer | |
| Пластик (3D-печать) | Plastic / filament | |
