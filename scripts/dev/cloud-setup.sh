#!/usr/bin/env bash
# BreezeCAD — подготовка облачной песочницы Claude (Linux x86_64, Ubuntu 24.04)
# к сборке, тестам и UI-сценариям на Xvfb.
#
# Зачем: в песочнице закрыты apt и PyPI, открыт только GitHub. Поэтому OCCT и
# GLFW собираются из исходников с GitHub, недостающие заголовки X11/GL берутся
# оттуда же. Повторный запуск ничего не пересобирает (метки *.done).
#
#   bash scripts/dev/cloud-setup.sh            # зависимости + сборка + тесты
#   bash scripts/dev/cloud-setup.sh deps       # только зависимости
#
# Быстрый путь: если в релизе `dev-deps` репозитория лежит архив
# bz-deps-linux-x64.tar.xz, он скачивается вместо часовой сборки OCCT.
# Выложить его: bash scripts/dev/cloud-setup.sh pack (нужен gh с правом пуша).
#
# Переменные: BZ_DEPS (по умолчанию $HOME/deps), BZ_JOBS (по умолчанию nproc).
set -euo pipefail

DEPS=${BZ_DEPS:-$HOME/deps}
PREFIX=$DEPS/prefix
SRC=$DEPS/src
JOBS=${BZ_JOBS:-$(nproc)}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
OCCT_TAG=V7_8_1
GLFW_TAG=3.4
ARCHIVE=bz-deps-linux-x64.tar.xz
mkdir -p "$SRC" "$PREFIX"

log() { printf '\n== %s\n' "$*"; }
clone() { # clone <github owner/repo> <dir> [tag]
    [ -d "$SRC/$2/.git" ] && return 0
    git clone -q --depth 1 ${3:+--branch "$3"} "https://github.com/$1" "$SRC/$2"
}

# --- 1. Заголовки X11 / OpenGL / EGL и ссылки *.so для линковки ------------
system_headers() {
    [ -f "$DEPS/headers.done" ] && return 0
    log "Заголовки X11/GL/EGL (zig-gamedev/system_sdk, NVIDIA/libglvnd)"
    clone zig-gamedev/system_sdk system_sdk
    clone NVIDIA/libglvnd libglvnd
    local S=$SRC/system_sdk/linux/include
    cp -r --update=none "$S/X11/." /usr/include/X11/
    cp -r --update=none "$S/xkbcommon" /usr/include/
    cp -r --update=none "$SRC/libglvnd/include/GL" "$SRC/libglvnd/include/EGL" \
        "$SRC/libglvnd/include/KHR" /usr/include/
    local L=/usr/lib/x86_64-linux-gnu
    for l in GL EGL GLX Xrandr Xinerama Xcursor Xi Xfixes; do
        [ -e "$L/lib$l.so" ] && continue
        local f; f=$(ls "$L"/lib$l.so.* 2>/dev/null | sort | head -1 || true)
        [ -n "$f" ] && ln -s "$f" "$L/lib$l.so"
    done
    touch "$DEPS/headers.done"
}

# --- 2. Готовый архив зависимостей из релиза (если есть) -------------------
fetch_prebuilt() {
    [ -f "$DEPS/occt.done" ] && return 0
    command -v gh >/dev/null || return 1
    log "Пробую готовые зависимости из релиза dev-deps"
    gh release download dev-deps -R MikeVerx/breezecad -p "$ARCHIVE" -D "$DEPS" --clobber 2>/dev/null || return 1
    tar -C "$DEPS" -xJf "$DEPS/$ARCHIVE" && rm -f "$DEPS/$ARCHIVE"
    touch "$DEPS/glfw.done" "$DEPS/occt.done"
}

# --- 3. GLFW ----------------------------------------------------------------
build_glfw() {
    [ -f "$DEPS/glfw.done" ] && return 0
    log "GLFW $GLFW_TAG"
    clone glfw/glfw glfw "$GLFW_TAG"
    cmake -S "$SRC/glfw" -B "$SRC/glfw/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=ON \
        -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
        -DBUILD_SHARED_LIBS=OFF >/dev/null
    cmake --build "$SRC/glfw/build" -j "$JOBS" >/dev/null
    cmake --install "$SRC/glfw/build" >/dev/null
    touch "$DEPS/glfw.done"
}

# --- 4. OCCT (только нужные модули, без Draw/TBB/FreeImage) -----------------
build_occt() {
    [ -f "$DEPS/occt.done" ] && return 0
    log "OCCT $OCCT_TAG — около часа на 2 ядрах"
    clone Open-Cascade-SAS/OCCT occt "$OCCT_TAG"
    cmake -S "$SRC/occt" -B "$SRC/occt/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DINSTALL_DIR="$PREFIX" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_ApplicationFramework=ON \
        -DBUILD_MODULE_DataExchange=ON -DBUILD_MODULE_Visualization=ON \
        -DBUILD_MODULE_ModelingAlgorithms=ON -DBUILD_MODULE_ModelingData=ON \
        -DBUILD_DOC_Overview=OFF -DUSE_TK=OFF -DUSE_TCL=OFF -DUSE_TBB=OFF \
        -DUSE_FREEIMAGE=OFF -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_VTK=OFF \
        -DUSE_FFMPEG=OFF -DUSE_OPENVR=OFF -DUSE_FREETYPE=ON -DUSE_OPENGL=ON \
        -DUSE_GLES2=OFF -DUSE_XLIB=ON >/dev/null
    cmake --build "$SRC/occt/build" -j "$JOBS"
    cmake --install "$SRC/occt/build" >/dev/null
    touch "$DEPS/occt.done"
}

# --- 5. Архив для релиза dev-deps -------------------------------------------
pack() {
    log "Упаковка $ARCHIVE"
    tar -C "$DEPS" -I 'xz -6 -T0' -cf "$DEPS/$ARCHIVE" prefix
    gh release view dev-deps -R MikeVerx/breezecad >/dev/null 2>&1 ||
        gh release create dev-deps -R MikeVerx/breezecad --prerelease \
            --title "Зависимости для облачной сборки (не для пользователей)" \
            --notes "OCCT $OCCT_TAG + GLFW $GLFW_TAG для Linux x86_64. Берёт scripts/dev/cloud-setup.sh."
    gh release upload dev-deps -R MikeVerx/breezecad "$DEPS/$ARCHIVE" --clobber
}

# --- 6. Сборка BreezeCAD и тесты --------------------------------------------
build_app() {
    log "Сборка BreezeCAD (linux-debug)"
    cd "$REPO"
    cmake --preset linux-debug -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DOpenCASCADE_DIR="$PREFIX/lib/cmake/opencascade" >/dev/null
    cmake --build --preset linux-debug -j "$JOBS" 2>&1 | grep -E 'error|warning:' | head -40 || true
    log "Тесты"
    LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}" ./build/linux-debug/bin/bz_tests 2>&1 | tail -3
}

env_hint() {
    cat <<EOF

Готово. В каждой новой оболочке для запуска программы:
  export LD_LIBRARY_PATH=$PREFIX/lib:\$LD_LIBRARY_PATH
UI-сценарии: scripts/ui-test/ui.sh (Xvfb уже стоит в образе).
EOF
}

case "${1:-all}" in
    deps) system_headers; fetch_prebuilt || { build_glfw; build_occt; }; env_hint ;;
    pack) pack ;;
    all)  system_headers; fetch_prebuilt || { build_glfw; build_occt; }; build_app; env_hint ;;
    *)    echo "usage: $0 [all|deps|pack]"; exit 2 ;;
esac
