# BreezeCAD — граф кода Graphify для Claude Code (Windows).
# Linux / macOS: scripts/dev/graphify-setup.sh. Подробности: docs/dev/AI-TOOLS.md
#
#   powershell -ExecutionPolicy Bypass -File scripts\dev\graphify-setup.ps1          # установить + граф + подключить
#   powershell -ExecutionPolicy Bypass -File scripts\dev\graphify-setup.ps1 update   # только перестроить граф
#   powershell -ExecutionPolicy Bypass -File scripts\dev\graphify-setup.ps1 remove   # отключить и удалить граф
#
# Граф строится только по коду, локально, без нейросети и без токенов.
# graphify-out\ и .claude\settings.local.json — в .gitignore: ни один
# отслеживаемый git файл скрипт не меняет. Нужен Python 3.10+ (python.org).
param([string]$Mode = 'all')
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $Repo

function Log($m) { Write-Host "`n== $m" -ForegroundColor Cyan }
function Die($m) { Write-Host "`nОШИБКА: $m" -ForegroundColor Red; exit 1 }

$script:PyCmd = $null; $script:PyPre = @()
function Find-Python {
    if ($script:PyCmd) { return $true }
    foreach ($c in @('py', 'python', 'python3')) {
        if (-not (Get-Command $c -CommandType Application -ErrorAction SilentlyContinue)) { continue }
        $pre = @(); if ($c -eq 'py') { $pre = @('-3') }
        & $c @pre -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>$null
        if ($LASTEXITCODE -eq 0) { $script:PyCmd = $c; $script:PyPre = $pre; return $true }
    }
    return $false
}
function Invoke-Py { $a = @($script:PyPre) + @($args); & $script:PyCmd @a }

function Find-Graphify {
    $g = Get-Command graphify -ErrorAction SilentlyContinue
    if ($g) { return $g.Source }
    if (Find-Python) {
        foreach ($scheme in @('nt_user', 'nt')) {
            $d = Invoke-Py -c "import sysconfig; print(sysconfig.get_path('scripts', '$scheme'))" 2>$null
            if ($d -and (Test-Path (Join-Path $d 'graphify.exe'))) { return (Join-Path $d 'graphify.exe') }
        }
    }
    return $null
}

function Install-Graphify {
    $exe = Find-Graphify
    if ($exe) { Log "Graphify уже установлен: $exe"; return }
    Log 'Установка Graphify (пакет graphifyy — с двумя y)'
    if (Get-Command uv -ErrorAction SilentlyContinue) { uv tool install graphifyy }
    elseif (Get-Command pipx -ErrorAction SilentlyContinue) { pipx install graphifyy }
    else {
        if (-not (Find-Python)) { Die 'нужен Python 3.10+ (python.org, галочка «Add to PATH»)' }
        Invoke-Py -m pip install --user graphifyy
        if ($LASTEXITCODE -ne 0) { Die 'не удалось установить с PyPI' }
    }
    if (-not (Find-Graphify)) { Die 'graphify установлен, но не найден — откройте новое окно PowerShell и повторите' }
}

function Build-Graph {
    $exe = Find-Graphify
    if (-not $exe) { Die 'graphify не установлен — запустите скрипт без аргументов' }
    Log 'Построение графа кода (локально, без нейросети)'
    $env:GRAPHIFY_BACKEND = 'none'
    & $exe extract . --code-only
    if (-not (Test-Path 'graphify-out\graph.json')) { Die 'граф не построился — см. вывод выше' }
    Log 'Готово: graphify-out\GRAPH_REPORT.md, graphify-out\graph.html (открыть в браузере)'
}

# Подсказка Claude Code: перед поиском по файлам — заглянуть в граф.
# Хук «fail-open»: при любой ошибке молчит и ничего не блокирует.
$HookPy = @'
import json, os, sys
mode, exe = sys.argv[1], sys.argv[2]
path = os.path.join('.claude', 'settings.local.json')
data = {}
if os.path.exists(path):
    with open(path, encoding='utf-8-sig') as f:
        data = json.load(f)
hooks = data.setdefault('hooks', {})
pre = [g for g in hooks.get('PreToolUse', []) if 'hook-guard' not in json.dumps(g)]
if mode == 'add':
    q = '"%s"' % exe if ' ' in exe else exe
    pre += [
        {"matcher": "Bash|Grep", "hooks": [{"type": "command", "command": q + " hook-guard search", "timeout": 10}]},
        {"matcher": "Read|Glob", "hooks": [{"type": "command", "command": q + " hook-guard read", "timeout": 10}]},
    ]
if pre:
    hooks['PreToolUse'] = pre
else:
    hooks.pop('PreToolUse', None)
if not hooks:
    data.pop('hooks', None)
os.makedirs('.claude', exist_ok=True)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    json.dump(data, f, ensure_ascii=True, indent=2)
    f.write('\n')
print('  .claude/settings.local.json: graphify hook ' + ('on' if mode == 'add' else 'off'))
'@

function Set-Hook($m) {
    if (-not (Find-Python)) { Die 'нужен Python 3.10+' }
    $exe = ''; if ($m -eq 'add') { $exe = Find-Graphify }
    $tmp = Join-Path ([IO.Path]::GetTempPath()) 'bz-graphify-hook.py'
    [IO.File]::WriteAllText($tmp, $HookPy)
    Invoke-Py $tmp $m $exe
    Remove-Item $tmp -ErrorAction SilentlyContinue
}

switch ($Mode) {
    'all'    { Install-Graphify; Build-Graph; Set-Hook 'add'; Log 'Перезапустите Claude Code в этой папке — он начнёт пользоваться графом' }
    'update' { Build-Graph }
    'remove' { Set-Hook 'remove'; Remove-Item -Recurse -Force graphify-out -ErrorAction SilentlyContinue
               Log 'Граф удалён, хук убран. Сам пакет: pip uninstall graphifyy' }
    default  { Die "неизвестный режим: $Mode (all | update | remove)" }
}
