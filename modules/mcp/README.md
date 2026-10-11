# MCP-сервер для Claude

Модуль BreezeCAD, через который Claude (и любой MCP-клиент) работает с открытым
в программе документом: читает дерево и параметры, создаёт эскизы, выдавливает,
вращает, меняет размеры, сохраняет и экспортирует. Каждое изменение — обычный
шаг отмены (Ctrl+Z), пользователь видит его сразу.

## Включить

1. BreezeCAD → **Настройки → Модули → «MCP-сервер для Claude» → Включить**.
   По умолчанию модуль выключен: он открывает порт (только на этом компьютере).
2. Модуль создаёт файл `mcp/connection.json` в папке настроек BreezeCAD:
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
      "command": "C:\\BreezeCAD\\modules\\mcp\\bz-mcp-bridge.exe"
    }
  }
}
```

Путь — к `bz-mcp-bridge.exe` из папки этого модуля. Мост сам читает адрес и ключ
из `connection.json`. Claude Desktop после правки конфига перезапустить целиком.
Если BreezeCAD не запущен, инструменты отвечают «запустите BreezeCAD».

## Claude Code

Напрямую по HTTP (ключ — из `connection.json`):

```sh
claude mcp add --transport http breezecad http://127.0.0.1:47321/mcp --header "Authorization: Bearer <token>"
```

или через тот же мост: `claude mcp add breezecad -- "C:/BreezeCAD/modules/mcp/bz-mcp-bridge.exe"`.

## Что умеет

Команды берутся из самой программы (`commands.list`), имена инструментов —
с `_` вместо точки:

| Группа | Инструменты |
| --- | --- |
| Документы | `app_documents`, `app_new_part`, `app_open`, `app_activate`, `doc_save`, `doc_export`, `doc_undo`, `doc_redo` |
| Деталь | `part_tree`, `part_feature`, `part_parameters`, `part_properties`, `part_set_parameter`, `part_set_variable`, `part_set_info`, `part_rename`, `part_suppress`, `part_delete` |
| Эскиз | `sketch_create`, `sketch_add`, `sketch_constrain`, `sketch_remove`, `sketch_get` |
| Тела | `feature_extrude`, `feature_revolve`, `feature_thread` (настоящая винтовая ISO-резьба: ось + диаметр) |

Пока пользователь редактирует эскиз или операцию, изменяющие команды
отказывают (программа не выдёргивает правку из рук).

## Безопасность

- Слушает только `127.0.0.1`; запросы без ключа — `401`; запросы из браузера
  (заголовок `Origin`) — `403`.
- Модуль может создавать и перезаписывать файлы (`doc_save`, `doc_export`) по
  путям, которые укажет клиент, — как любая команда «Сохранить как».
