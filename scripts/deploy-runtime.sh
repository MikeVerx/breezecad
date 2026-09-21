#!/usr/bin/env bash
# scripts/deploy-runtime.sh
#
# Определяет runtime-зависимости breezecad.exe через objdump -p
# и копирует в bin/ только те DLL, которые реально нужны.
# Лишние DLL (которых нет в зависимостях) удаляются из bin/.
#
# Аргументы:
#   $1  — папка bin (куда копировать и что чистить)
#   $2  — имя exe-файла
#   $3  — путь к папке OCCT bin (опционально; default: $HOME/occt/install/win64/gcc/bin)
#
# Требует: objdump (binutils из MSYS2).

set -e

BIN_DIR="${1:?usage: $0 <bin_dir> <exe_name> [occt_bin_dir]}"
EXE_NAME="${2:?usage: $0 <bin_dir> <exe_name> [occt_bin_dir]}"
OCCT_BIN="${3:-$HOME/occt/install/win64/gcc/bin}"
MINGW_BIN="/mingw64/bin"

EXE="$BIN_DIR/$EXE_NAME"

if [ ! -d "$BIN_DIR" ]; then
    echo "ERROR: bin dir not found: $BIN_DIR" >&2
    exit 1
fi
if [ ! -f "$EXE" ]; then
    echo "ERROR: exe not found: $EXE" >&2
    exit 1
fi
if ! command -v objdump >/dev/null 2>&1; then
    echo "ERROR: objdump not found. Install binutils." >&2
    exit 1
fi

# Системные DLL Windows — не копируем и не ищем.
# Шаблоны с * — как в bash-[[ ]] (glob).
SYSTEM_PATTERNS=(
    'kernel32.dll' 'user32.dll' 'gdi32.dll' 'advapi32.dll' 'shell32.dll'
    'ole32.dll' 'oleaut32.dll' 'comdlg32.dll' 'comctl32.dll' 'ws2_32.dll'
    'opengl32.dll' 'glu32.dll' 'winmm.dll' 'imm32.dll' 'setupapi.dll'
    'uxtheme.dll' 'dwmapi.dll' 'version.dll' 'shlwapi.dll' 'crypt32.dll'
    'bcrypt.dll' 'secur32.dll' 'netapi32.dll' 'userenv.dll' 'mpr.dll'
    'winspool.drv' 'd3d11.dll' 'dxgi.dll' 'd3d9.dll' 'd3d12.dll'
    'vulkan-1.dll' 'cfgmgr32.dll' 'rpcrt4.dll' 'shcore.dll' 'psapi.dll'
    'api-ms-win-*' 'ext-ms-*' 'ucrtbase.dll' 'vcruntime*.dll' 'msvcp*.dll'
    'mswsock.dll' 'dnsapi.dll' 'iphlpapi.dll' 'ncrypt.dll' 'bcryptprimitives.dll'
    'normaliz.dll' 'wintrust.dll' 'dbghelp.dll'
)

is_system() {
    local name_lc
    name_lc=$(echo "$1" | tr 'A-Z' 'a-z')
    local pat
    for pat in "${SYSTEM_PATTERNS[@]}"; do
        # shellcheck disable=SC2053
        if [[ "$name_lc" == $pat ]]; then
            return 0
        fi
    done
    return 1
}

# Найти DLL по имени. Ищем в BIN_DIR (уже скопированные), затем в OCCT, затем в MinGW.
find_dll() {
    local name="$1"
    local dir
    for dir in "$BIN_DIR" "$OCCT_BIN" "$MINGW_BIN"; do
        if [ -f "$dir/$name" ]; then
            echo "$dir/$name"
            return 0
        fi
    done
    return 1
}

# ===== BFS по зависимостям =====
declare -A visited
declare -a queue
declare -a needed
queue=("$EXE_NAME")

while [ ${#queue[@]} -gt 0 ]; do
    cur="${queue[0]}"
    queue=("${queue[@]:1}")

    if [ -n "${visited[$cur]:-}" ]; then
        continue
    fi
    visited[$cur]=1

    # Полный путь к текущему бинарнику
    full=""
    if [ "$cur" = "$EXE_NAME" ]; then
        full="$EXE"
    else
        full=$(find_dll "$cur" || true)
    fi
    [ -z "$full" ] && continue
    [ ! -f "$full" ] && continue

    if [ "$cur" != "$EXE_NAME" ] && ! is_system "$cur"; then
        needed+=("$cur")
    fi

    # Парсим прямые зависимости
    deps=$(objdump -p "$full" 2>/dev/null | grep -i 'DLL Name:' | awk '{print $3}')
    for d in $deps; do
        if [ -z "${visited[$d]:-}" ]; then
            queue+=("$d")
        fi
    done
done

echo "== deploy-runtime =="
echo "  bin dir:  $BIN_DIR"
echo "  occt bin: $OCCT_BIN"
echo "  required: ${#needed[@]} DLLs"

# ===== Копируем нужные =====
copied=0
for dll in "${needed[@]}"; do
    src=$(find_dll "$dll" || true)
    if [ -z "$src" ]; then
        echo "  ! not found: $dll" >&2
        continue
    fi
    dst="$BIN_DIR/$dll"
    if [ ! -f "$dst" ] || [ "$src" -nt "$dst" ]; then
        cp -f "$src" "$dst"
        copied=$((copied+1))
    fi
done
echo "  copied/updated: $copied"

# ===== Удаляем неиспользуемые =====
# Удаляем только те DLL, что есть в OCCT_BIN или MINGW_BIN (наши), но не в needed.
removed=0
shopt -s nullglob
for f in "$BIN_DIR"/*.dll; do
    name=$(basename "$f")
    skip=0
    for nd in "${needed[@]}"; do
        if [ "$name" = "$nd" ]; then
            skip=1
            break
        fi
    done
    [ "$skip" = 1 ] && continue

    if [ -f "$OCCT_BIN/$name" ] || [ -f "$MINGW_BIN/$name" ]; then
        rm -f "$f"
        removed=$((removed+1))
    fi
done
shopt -u nullglob
echo "  removed unused: $removed"
echo "Done."
