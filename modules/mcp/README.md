# MCP-сервер для Claude

Модуль BreezeCAD, через который Claude (и любой MCP-клиент) работает с открытым
в программе документом: эскизы, все операции детали, грани и рёбра, рабочая
геометрия, картинка модели, внешний вид, стандартные изделия, сборки. Каждое
изменение — обычный шаг отмены (Ctrl+Z), пользователь видит его сразу.

## Установить

1. Скачайте файл модуля для своей системы со страницы
   [«Модули BreezeCAD»](https://github.com/MikeVerx/breezecad/releases/tag/modules):
   `bz-mcp-<версия>-windows-x64.bzmod` (Linux — `…-linux-x64.bzmod`, macOS — `…-macos-arm64.bzmod`).
   Нужен BreezeCAD 0.2.1 или новее.
2. BreezeCAD → **Настройки → Модули → «Установить модуль…»** и выберите файл —
   или просто перетащите его в окно программы. Модуль заработает сразу, без
   перезапуска; выключить или удалить его можно там же.

   Модуль открывает порт — только на этом компьютере (`127.0.0.1`) и только по ключу.
3. Модуль создаёт файл `mcp/connection.json` в папке настроек BreezeCAD:
   - Windows: `%APPDATA%\BreezeCAD\mcp\connection.json`
   - Linux: `~/.config/breezecad/mcp/connection.json`
   - macOS: `~/Library/Application Support/BreezeCAD/mcp/connection.json`

   В нём адрес (`http://127.0.0.1:47321/mcp`) и ключ (`token`). Порт можно
   поменять в этом же файле и перезапустить BreezeCAD.

## Claude Desktop

`claude_desktop_config.json` (Settings → Developer → Edit Config):

```json
{
  "mcpServers": {
    "breezecad": {
      "command": "C:\\Users\\ИМЯ\\AppData\\Roaming\\BreezeCAD\\modules\\mcp\\bz-mcp-bridge.exe"
    }
  }
}
```

Путь — к `bz-mcp-bridge.exe` в папке установленного модуля: `%APPDATA%\BreezeCAD\modules\mcp\`
(Linux — `~/.config/breezecad/modules/mcp/bz-mcp-bridge`; «Настройки → Модули → Папка модулей»
открывает эту папку). Мост сам читает адрес и ключ
из `connection.json`. Claude Desktop после правки конфига перезапустить целиком.
Если BreezeCAD не запущен, инструменты отвечают «запустите BreezeCAD».

## Claude Code

Напрямую по HTTP (ключ — из `connection.json`):

```sh
claude mcp add --transport http breezecad http://127.0.0.1:47321/mcp --header "Authorization: Bearer <token>"
```

или через тот же мост: `claude mcp add breezecad -- "%APPDATA%/BreezeCAD/modules/mcp/bz-mcp-bridge.exe"`.

## Собрать самому

```sh
cmake --preset linux-release -DBZ_BUILD_MCP=ON
cmake --build --preset linux-release
# → build/linux-release/packages/bz-mcp-<версия>-linux-x64.bzmod
```

## Что умеет

Команды берутся из самой программы (`commands.list`), имена инструментов —
с `_` вместо точки:

| Группа | Инструменты |
| --- | --- |
| Документы | `app_documents`, `app_new_part`, `app_open`, `app_activate`, `doc_save`, `doc_export`, `doc_undo`, `doc_redo`, `doc_close` |
| Деталь | `part_tree`, `part_feature`, `part_parameters`, `part_properties`, `part_set_parameter`, `part_set_variable`, `part_set_info`, `part_rename`, `part_suppress`, `part_delete`, `part_move_feature`, `part_set_end_marker` |
| Эскиз | `sketch_create` (на плоскости, рабочей плоскости или плоской грани), `sketch_add`, `sketch_constrain`, `sketch_remove`, `sketch_get` |
| Грани и рёбра | `part_faces`, `part_edges` (номера с фильтрами: нормаль, тип, радиус, «ближе к точке»), `part_measure` |
| Тела из эскизов | `feature_extrude`, `feature_revolve`, `feature_sweep`, `feature_loft`, `feature_coil` |
| Операции | `feature_hole` (простое, цековка, зенковка), `feature_thread` (винтовая ISO-резьба), `feature_fillet`, `feature_chamfer`, `feature_shell`, `feature_pattern` (прямоугольный и круговой), `feature_mirror`, `feature_section` |
| Любая операция | `part_feature_types`, `part_create_feature`, `part_edit_feature` — по полям, как в файле .bzc (и для операций будущих модулей) |
| Рабочая геометрия | `work_plane` (смещение, середина, угол, три точки, касательная), `work_axis`, `work_point` |
| Вид | `view_snapshot` — картинка модели (PNG; модель её видит), `view_set` — камера и стиль |
| Внешний вид | `appearance_list`, `part_set_appearance` (телу или граням; материал или цвет) |
| Библиотека | `library_list`, `library_sizes`, `library_make_part` — стандартные изделия ГОСТ/ISO/DIN |
| Сборка | `assembly_new`, `assembly_components`, `assembly_place`, `assembly_faces`, `assembly_constrain` (зависимости и соединения), `assembly_edit_constraint`, `assembly_move`, `assembly_ground`, `assembly_remove`, `assembly_clashes` |

Грани и рёбра называются номерами из `part_faces` / `part_edges` (в сборке —
`assembly_faces`): номер действует до следующего изменения детали, а в
операцию записывается ссылка, которая находит грань после перестроения — как
щелчок мышью. Операция, которая не построилась, сразу отменяется, и в ответе
приходит причина.

Пока пользователь редактирует эскиз или операцию, изменяющие команды
отказывают (программа не выдёргивает правку из рук).

## Безопасность

- Слушает только `127.0.0.1`; запросы без ключа — `401`; запросы из браузера
  (заголовок `Origin`) — `403`.
- Модуль может создавать и перезаписывать файлы (`doc_save`, `doc_export`) по
  путям, которые укажет клиент, — как любая команда «Сохранить как».
