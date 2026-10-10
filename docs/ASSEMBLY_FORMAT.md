# Файл сборки

Сборка хранится в том же контейнере `.bzc`, что и деталь (ZIP: `manifest.json` +
`document.json`). Вид документа — поле `documentType` манифеста; список видов —
`document/DocumentTypes.*` (`part`, `assembly`, `drawing` — последний пока
заглушка). Файлы без `documentType` считаются деталями.

Детали в сборку **не копируются**: компонент ссылается на файл детали путём
относительно файла сборки (разделитель `/`). Пока сборка не сохранена, путь
абсолютный; при первом сохранении и «Сохранить как» пути пересчитываются
(`RebasePaths`).

```json
{
  "assembly": { "name": "Узел", "designation": "БРЗ.100.000" },
  "formatVersion": 1,
  "nextId": 4,
  "components": [
    { "id": 1, "name": "Основание:1", "path": "Детали/Основание.bzc",
      "placement": { "origin": [0,0,0], "xAxis": [1,0,0], "zAxis": [0,0,1] },
      "properties": { "designation": "", "name": "", "material": "", "quantity": 1 },
      "grounded": true }
  ],
  "constraints": [
    { "id": 3, "name": "Вставка:1", "type": "insert", "value": 0, "flipped": false,
      "first":  { "component": 1, "kind": "circle", "origin": [..], "direction": [..], "radius": 4, "pick": [..] },
      "second": { "component": 2, "kind": "circle", ... } }
  ]
}
```

- `placement` — оси детали в координатах сборки (как у рабочей плоскости), без углов Эйлера.
- `properties` — для будущей спецификации; пустое поле = взять из свойств детали.
- Типы зависимостей: `mate` (совмещение; `flipped` = заподлицо), `insert` (вставка;
  `flipped` = соосно в одну сторону), `angle` (`value` в радианах), `tangent`
  (`flipped` = внутреннее касание), `axisMate` (совпадение осей). `value` — зазор, мм.
- Геометрия (`kind`: plane, axis, point, cylinder, sphere, circle) хранится в
  координатах детали; `pick` — точка щелчка, по ней геометрия ищется снова, когда
  деталь изменилась.
- Неизвестный тип зависимости пропускается с предупреждением; повреждённый компонент —
  ошибка открытия.
