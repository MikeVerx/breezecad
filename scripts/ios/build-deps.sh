#!/usr/bin/env bash
# Builds what the BreezeCAD core needs on iOS and iPadOS: FreeType and Open
# CASCADE, as static libraries for one SDK (an app may not carry loose
# .dylib files). About an hour per SDK on a GitHub macOS runner; CI keeps the
# result in the Actions cache (ios.yml). Same versions and modules as
# scripts/android/build-deps.sh.
#
# Usage: build-deps.sh <sdk> <prefix>
#   sdk     iphoneos (iPhone and iPad, arm64) or iphonesimulator (the
#           simulator on an Apple Silicon Mac, arm64: the tests and the
#           smoke run)
#   prefix  where to install: <prefix>/lib/*.a, <prefix>/lib/cmake/opencascade
# Needs: Xcode (xcrun), cmake, ninja, git.
#
# The versions below are part of the CI cache key (the hash of this file):
# change one and the next run builds everything again.
set -euo pipefail

SDK=${1:?sdk: iphoneos or iphonesimulator}
mkdir -p "${2:?install prefix}"
PREFIX=$(cd "$2" && pwd)

OCCT_TAG=V8_0_1            # the same OCCT as the other packages
FREETYPE_TAG=VER-2-13-3
IOS_MIN=16.3               # std::format needs iOS 16.3; keep in step with CMakePresets.json
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
SRC=${SRC:-$PREFIX/../src}

case "$SDK" in
    iphoneos|iphonesimulator) ;;
    *) echo "unknown SDK $SDK (iphoneos or iphonesimulator)" >&2; exit 1 ;;
esac
SYSROOT=$(xcrun --sdk "$SDK" --show-sdk-path)

# One set of toolchain flags for both libraries. arm64 only: every iPad and
# iPhone that runs iOS 16 is arm64, and so is the simulator on the runner.
IOS_ARGS=(
    -G Ninja -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SYSROOT"
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN"
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
)

log() { echo "== $*"; }
clone() { [ -d "$SRC/$2/.git" ] || git clone -q --depth 1 --branch "$3" "https://github.com/$1" "$SRC/$2"; }

mkdir -p "$SRC"

# --- FreeType: fonts for OCCT's text (kernel/TextGeometry) ------------------
# Nothing optional (no zlib, png, HarfBuzz, Brotli): OCCT only reads outlines.
log "FreeType $FREETYPE_TAG ($SDK)"
clone freetype/freetype freetype "$FREETYPE_TAG"
cmake -S "$SRC/freetype" -B "$SRC/freetype/build-$SDK" "${IOS_ARGS[@]}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON >/dev/null
cmake --build "$SRC/freetype/build-$SDK" -j "$JOBS" >/dev/null
cmake --install "$SRC/freetype/build-$SDK" >/dev/null

# --- Open CASCADE -----------------------------------------------------------
# The modules the core links (cmake/OpenCascade.cmake). On iOS OCCT draws
# with OpenGL ES (TKOpenGles, EAGL), as on Android.
log "OCCT $OCCT_TAG ($SDK) — около часа"
clone Open-Cascade-SAS/OCCT occt "$OCCT_TAG"
cmake -S "$SRC/occt" -B "$SRC/occt/build-$SDK" "${IOS_ARGS[@]}" \
    -DINSTALL_DIR="$PREFIX" -DINSTALL_DIR_LAYOUT=Unix -DBUILD_LIBRARY_TYPE=Static \
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
    -D3RDPARTY_FREETYPE_LIBRARY="$PREFIX/lib/libfreetype.a" >/dev/null
cmake --build "$SRC/occt/build-$SDK" -j "$JOBS"
cmake --install "$SRC/occt/build-$SDK" >/dev/null

# The folder of OpenCASCADEConfig.cmake differs between OCCT versions.
config=$(dirname "$(find "$PREFIX" -name OpenCASCADEConfig.cmake | head -1)")
[ -n "$config" ] && [ -d "$config" ] || { echo "OpenCASCADEConfig.cmake not installed" >&2; exit 1; }
[ "$config" = "$PREFIX/lib/cmake/opencascade" ] || ln -sfn "$config" "$PREFIX/lib/cmake/opencascade"
log "Готово: $PREFIX ($(ls "$PREFIX"/lib/*.a | wc -l | tr -d ' ') библиотек, $(du -sh "$PREFIX" | cut -f1))"
