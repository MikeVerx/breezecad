#!/usr/bin/env bash
# BreezeCAD — граф кода Graphify для Claude Code (Linux / macOS).
# Windows: scripts/dev/graphify-setup.ps1. Подробности: docs/dev/AI-TOOLS.md
#
#   bash scripts/dev/graphify-setup.sh          # установить + построить граф + подключить к Claude Code
#   bash scripts/dev/graphify-setup.sh update   # только перестроить граф (после больших правок)
#   bash scripts/dev/graphify-setup.sh remove   # отключить от Claude Code и удалить граф
#
# Что делает и чего НЕ делает:
#  - строит граф ТОЛЬКО по коду (tree-sitter, локально): без нейросети, без ключей,
#    без затрат токенов, исходники никуда не уходят (GRAPHIFY_BACKEND=none, --code-only);
#  - результат кладёт в graphify-out/ — папка в .gitignore, в коммиты не попадает;
#  - подсказку для Claude Code пишет в .claude/settings.local.json — личный файл,
#    тоже в .gitignore. Ни один отслеживаемый git файл скрипт не меняет.
#
# Нужен Python 3.10+ и доступ к PyPI. В облачной песочнице Claude PyPI закрыт —
# там скрипт сообщит об этом и ничего не сломает.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
MODE=${1:-all}
cd "$REPO"

log() { printf '\n== %s\n' "$*"; }
die() { printf '\nОШИБКА: %s\n' "$*" >&2; exit 1; }

find_graphify() {
    command -v graphify 2>/dev/null && return 0
    for p in "$HOME/.local/bin/graphify" "$HOME/.cargo/bin/graphify"; do
        [ -x "$p" ] && { echo "$p"; return 0; }
    done
    return 1
}

install_graphify() {
    if find_graphify >/dev/null; then
        log "Graphify уже установлен: $(find_graphify)"
        return 0
    fi
    log "Установка Graphify (пакет graphifyy — с двумя y)"
    command -v python3 >/dev/null || die "нужен Python 3.10+"
    python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' \
        || die "нужен Python 3.10+, сейчас $(python3 --version)"
    if command -v uv >/dev/null; then
        uv tool install graphifyy
    elif command -v pipx >/dev/null; then
        pipx install graphifyy
    else
        python3 -m pip install --user graphifyy \
            || python3 -m pip install --user --break-system-packages graphifyy \
            || die "не удалось установить с PyPI (нет сети или PyPI закрыт)"
    fi
    find_graphify >/dev/null || die "graphify установлен, но не найден в PATH (добавьте ~/.local/bin в PATH)"
}

build_graph() {
    local exe; exe=$(find_graphify) || die "graphify не установлен — запустите скрипт без аргументов"
    log "Построение графа кода (локально, без нейросети)"
    GRAPHIFY_BACKEND=none "$exe" extract . --code-only
    [ -f graphify-out/graph.json ] || die "граф не построился — см. вывод выше"
    log "Готово: graphify-out/GRAPH_REPORT.md, graphify-out/graph.html (открыть в браузере)"
}

# Подсказка Claude Code: перед поиском по файлам — заглянуть в граф.
# Хук «fail-open»: при любой ошибке молчит и ничего не блокирует.
hook_settings() { # hook_settings add|remove
    local exe=""
    [ "$1" = add ] && exe=$(find_graphify)
    python3 - "$1" "$exe" <<'PYEOF'
import json, os, sys
mode, exe = sys.argv[1], sys.argv[2]
path = os.path.join('.claude', 'settings.local.json')
data = {}
if os.path.exists(path):
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
hooks = data.setdefault('hooks', {})
pre = [g for g in hooks.get('PreToolUse', [])
       if 'hook-guard' not in json.dumps(g)]
if mode == 'add':
    q = '"%s"' % exe if ' ' in exe else exe
    pre += [
        {"matcher": "Bash|Grep",
         "hooks": [{"type": "command", "command": q + " hook-guard search", "timeout": 10}]},
        {"matcher": "Read|Glob",
         "hooks": [{"type": "command", "command": q + " hook-guard read", "timeout": 10}]},
    ]
if pre:
    hooks['PreToolUse'] = pre
else:
    hooks.pop('PreToolUse', None)
if not hooks:
    data.pop('hooks', None)
os.makedirs('.claude', exist_ok=True)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write('\n')
print('  .claude/settings.local.json: хук Graphify', 'подключён' if mode == 'add' else 'убран')
PYEOF
}

case "$MODE" in
    all)    install_graphify; build_graph; hook_settings add
            log "Перезапустите Claude Code в этой папке — он начнёт пользоваться графом" ;;
    update) build_graph ;;
    remove) hook_settings remove; rm -rf graphify-out
            log "Граф удалён, хук убран. Сам пакет: uv tool uninstall graphifyy / pipx uninstall graphifyy" ;;
    *)      die "неизвестный режим: $MODE (all | update | remove)" ;;
esac
