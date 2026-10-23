# AI-инструменты для работы над BreezeCAD

Что подключено к репозиторию, чтобы Claude Code (и другие ассистенты) работали
быстрее и точнее. Всё необязательно: без этих инструментов проект собирается
и разрабатывается как раньше.

| Инструмент | Зачем | Где настроен | Что попадает в git |
|---|---|---|---|
| **Context7** | Свежая документация библиотек (Dear ImGui, nlohmann/json, doctest, CMake и т.д.) прямо в контекст — меньше кода по устаревшему API | `.mcp.json` | только `.mcp.json` |
| **Graphify** | Граф связей кода: кто кого вызывает, что от чего зависит. Агент сначала смотрит граф, а не перечитывает файлы — меньше токенов, быстрее ориентация | `scripts/dev/graphify-setup.*`, `.graphifyignore` | только скрипты и `.graphifyignore`; сам граф — нет |

MCP-модуль самой программы (`modules/mcp`) к этому отношения не имеет и не трогается.

---

## Context7

Ничего ставить не нужно. При первом запуске Claude Code в папке репозитория
спросит, доверять ли серверу `context7` из `.mcp.json` — ответить «да».

Чтобы агент точно сходил в документацию, достаточно дописать в запрос
`use context7`, например: «как в ImGui сделать таблицу с прокруткой, use context7».

Покрытие OpenCASCADE в каталоге Context7 не проверено — по OCCT агент может
по-прежнему опираться на `docs/dev/WORKLOG.md` и собственные знания.

Ключ API не нужен. Если упрётесь в лимиты — бесплатный ключ на context7.com,
но в `.mcp.json` его **не вписывать** (файл в git). Личный вариант с ключом:

```bash
claude mcp add --scope user --transport http --header "Authorization: Bearer ВАШ_КЛЮЧ" context7 https://mcp.context7.com/mcp
```

---

## Graphify

Строит граф **только по коду** (`src/`, `modules/`, `tests/`, `tools/`, `scripts/`),
локально через tree-sitter: без нейросети, без ключей и без затрат токенов,
исходники никуда не уходят. Что исключено — в `.graphifyignore`.

Результат — папка `graphify-out/` (в `.gitignore`):
- `GRAPH_REPORT.md` — сводка: центральные узлы, кластеры модулей;
- `graph.html` — интерактивный граф, открывается в браузере;
- `graph.json` — сам граф, его читает агент.

Подсказка для Claude Code («граф есть — сначала загляни в него») пишется в
`.claude/settings.local.json` — это личный файл, тоже в `.gitignore`. Поэтому у
тех, кто Graphify не ставил, ничего не меняется и ничего не ломается.

Нужен Python 3.10+ (на Windows — с python.org, галочка «Add to PATH»).

### Windows

Установить, построить граф и подключить к Claude Code:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\dev\graphify-setup.ps1
```

Перестроить граф после больших правок:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\dev\graphify-setup.ps1 update
```

Отключить и удалить граф:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\dev\graphify-setup.ps1 remove
```

### Linux / macOS

```bash
bash scripts/dev/graphify-setup.sh
```

```bash
bash scripts/dev/graphify-setup.sh update
```

```bash
bash scripts/dev/graphify-setup.sh remove
```

После установки — перезапустить Claude Code в папке репозитория.

### Проверка, что всё на месте

Граф построен:

```bash
ls graphify-out
```

Git ничего лишнего не видит (граф и личные настройки не должны появиться в списке):

```bash
git status --short
```

### Облачные сессии Claude (claude.ai/code)

В облачной песочнице по умолчанию закрыт PyPI (см. `scripts/dev/cloud-setup.sh`),
поэтому Graphify там не установится — скрипт честно напишет об ошибке и ничего
не сломает. Context7 там тоже может быть недоступен. Чтобы включить, в настройках
claude.ai нужно разрешить домены `pypi.org`, `files.pythonhosted.org` и
`mcp.context7.com` (Settings → Capabilities). Без этого облачные сессии работают
как раньше.

---

## Что рассматривали и не подключили

- **21st.dev Magic MCP** — генерирует веб-компоненты React; к C++/ImGui-интерфейсу
  не подходит. Может пригодиться для сайта проекта.
- **Strix** — ИИ-пентестер для веб-приложений и API; десктопному САПР почти
  ничего не даст, а агентов гоняет за счёт API-ключа. Вернуться при появлении
  сервера (каталог модулей, веб-версия).
- **Agent Skills** (сборник общих инженерных привычек для агента) — правила
  проекта уже описаны в `docs/RULES.md`, `docs/ARCHITECTURE.md` и `docs/ADR/`,
  они конкретнее общих советов.
