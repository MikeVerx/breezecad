#!/usr/bin/env bash
# Builds what the BreezeCAD core needs on Android: FreeType and Open CASCADE,
# as shared libraries for one ABI, with the NDK. About an hour per ABI on a
# GitHub runner; CI keeps the result in the Actions cache (android.yml).
#
# Usage: build-deps.sh <abi> <prefix>
#   abi     arm64-v8a (phones) or x86_64 (the emulator the tests run on)
#   prefix  where to install: <prefix>/lib/*.so, <prefix>/lib/cmake/opencascade
# Needs: ANDROID_NDK_HOME, cmake, ninja, git.
#
# The versions below are part of the CI cache key (the hash of this file):
# change one and the next run builds everything again.
set -euo pipefail

ABI=${1:?abi: arm64-v8a or x86_64}
PREFIX=$(realpath -m "${2:?install prefix}")
: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to the NDK folder}"

OCCT_TAG=V8_0_1            # the same OCCT as the Linux and Windows packages
FREETYPE_TAG=VER-2-13-3
API=28                     # Android 9; keep in step with CMakePresets.json
JOBS=${JOBS:-$(nproc)}
SRC=${SRC:-$PREFIX/../src}

# One set of toolchain flags for both libraries. c++_shared: OCCT is many
# shared libraries, each with its own static C++ runtime would not work.
# Flexible page sizes: the libraries load on 16 KB page devices too.
ANDROID_ARGS=(
    -G Ninja -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
    -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" -DANDROID_STL=c++_shared
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
)

log() { echo "== $*"; }
clone() { [ -d "$SRC/$2/.git" ] || git clone -q --depth 1 --branch "$3" "https://github.com/$1" "$SRC/$2"; }

mkdir -p "$SRC" "$PREFIX"

# --- FreeType: fonts for OCCT's text (kernel/TextGeometry) ------------------
# Shared, because OCCT's CMake looks for libfreetype.so; nothing optional
# (no zlib, png, HarfBuzz, Brotli): OCCT only reads outlines from font files.
log "FreeType $FREETYPE_TAG ($ABI)"
clone freetype/freetype freetype "$FREETYPE_TAG"
cmake -S "$SRC/freetype" -B "$SRC/freetype/build-$ABI" "${ANDROID_ARGS[@]}" \
    -DBUILD_SHARED_LIBS=ON \
    -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON >/dev/null
cmake --build "$SRC/freetype/build-$ABI" -j "$JOBS" >/dev/null
cmake --install "$SRC/freetype/build-$ABI" >/dev/null

# --- Open CASCADE -----------------------------------------------------------
# The modules the core links (cmake/OpenCascade.cmake): modeling, data
# exchange (STEP/STL, which needs the application framework for XCAF) and
# visualization (TKService/TKV3d for text; on Android OCCT draws with GLES,
# which step 2 of the port needs anyway). No Draw, TBB, FreeImage and others.
log "OCCT $OCCT_TAG ($ABI) — около часа"
clone Open-Cascade-SAS/OCCT occt "$OCCT_TAG"
cmake -S "$SRC/occt" -B "$SRC/occt/build-$ABI" "${ANDROID_ARGS[@]}" \
    -DINSTALL_DIR="$PREFIX" -DINSTALL_DIR_LAYOUT=Unix -DBUILD_LIBRARY_TYPE=Shared \
    -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_ApplicationFramework=ON \
    -DBUILD_MODULE_DataExchange=ON -DBUILD_MODULE_Visualization=ON \
    -DBUILD_MODULE_ModelingAlgorithms=ON -DBUILD_MODULE_ModelingData=ON \
    -DBUILD_DOC_Overview=OFF -DUSE_TK=OFF -DUSE_TCL=OFF -DUSE_TBB=OFF \
    -DUSE_FREEIMAGE=OFF -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_VTK=OFF \
    -DUSE_FFMPEG=OFF -DUSE_OPENVR=OFF -DUSE_FREETYPE=ON \
    -D3RDPARTY_FREETYPE_DIR="$PREFIX" \
    -D3RDPARTY_FREETYPE_INCLUDE_DIR_ft2build="$PREFIX/include/freetype2" \
    -D3RDPARTY_FREETYPE_INCLUDE_DIR_freetype2="$PREFIX/include/freetype2" \
    -D3RDPARTY_FREETYPE_LIBRARY_DIR="$PREFIX/lib" \
    -D3RDPARTY_FREETYPE_LIBRARY="$PREFIX/lib/libfreetype.so" >/dev/null
cmake --build "$SRC/occt/build-$ABI" -j "$JOBS"
cmake --install "$SRC/occt/build-$ABI" >/dev/null

# The folder of OpenCASCADEConfig.cmake differs between OCCT versions.
config=$(dirname "$(find "$PREFIX" -name OpenCASCADEConfig.cmake | head -1)")
[ -n "$config" ] && [ -d "$config" ] || { echo "OpenCASCADEConfig.cmake not installed" >&2; exit 1; }
[ "$config" = "$PREFIX/lib/cmake/opencascade" ] || ln -sfn "$config" "$PREFIX/lib/cmake/opencascade"
log "Готово: $PREFIX ($(ls "$PREFIX"/lib/*.so | wc -l) библиотек, $(du -sh "$PREFIX" | cut -f1))"
