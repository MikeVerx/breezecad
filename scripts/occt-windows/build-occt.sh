#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# A slim Open CASCADE for the Windows package, built in MSYS2 MinGW64.
#
# MSYS2's own OCCT package is built with FFmpeg, FreeImage, VTK, TBB, OpenVR
# and Tcl/Tk. BreezeCAD uses none of them, but the loader still needs every
# DLL they bring (video codecs, crypto, librsvg…): ~140 MB of the 220 MB the
# Windows build took. This one is the same OCCT version with the same bugfix
# patches MSYS2 applies, and only FreeType as a third-party library.
#
# Patches (patches/) are taken unchanged from msys2/MINGW-packages,
# mingw-w64-opencascade at commit 01aee637c05cb1a24508a2112ef19522b50cbbd5;
# the ones for FFmpeg, TBB and static builds are left out (not used here).
#
# Usage: build-occt.sh <install dir>
set -euo pipefail

OCCT_TAG="V7_9_3"   # the version MSYS2 ships now: same kernel as before
PREFIX="${1:?usage: $0 <install dir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"

git clone -q --depth 1 --branch "$OCCT_TAG" https://github.com/Open-Cascade-SAS/OCCT "$WORK/src"
for p in "$HERE"/patches/*.patch; do
    echo "patch: $(basename "$p")"
    patch -d "$WORK/src" -p1 -s < "$p"
done

# Only what the program links: no Draw test harness, no DE tools; every
# optional third-party library off except FreeType (text in the viewer).
cmake -G Ninja -S "$WORK/src" -B "$WORK/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DINSTALL_DIR_LAYOUT=Unix \
    -DBUILD_LIBRARY_TYPE=Shared \
    -DBUILD_MODULE_Draw=OFF \
    -DBUILD_MODULE_DETools=OFF \
    -DBUILD_DOC_Overview=OFF \
    -DUSE_FREETYPE=ON \
    -D3RDPARTY_FREETYPE_DIR=/mingw64 \
    -D3RDPARTY_FREETYPE_DLL_DIR=/mingw64/bin \
    -D3RDPARTY_FREETYPE_LIBRARY_DIR=/mingw64/lib \
    -D3RDPARTY_FREETYPE_INCLUDE_DIR_freetype2=/mingw64/include/freetype2 \
    -D3RDPARTY_FREETYPE_INCLUDE_DIR_ft2build=/mingw64/include/freetype2 \
    -D3RDPARTY_FREETYPE_LIBRARY=/mingw64/lib/libfreetype.dll.a \
    -D3RDPARTY_FREETYPE_DLL=/mingw64/bin/libfreetype-6.dll \
    -DUSE_FFMPEG=OFF -DUSE_FREEIMAGE=OFF -DUSE_OPENVR=OFF -DUSE_TBB=OFF \
    -DUSE_VTK=OFF -DUSE_TK=OFF -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_D3D=OFF

cmake --build "$WORK/build" -j 4
cmake --install "$WORK/build" > /dev/null
# Release DLLs without symbols: same as MSYS2's packages.
strip --strip-unneeded "$PREFIX"/bin/*.dll
rm -rf "$WORK"

echo "::notice title=Slim OCCT::$OCCT_TAG built: $(ls "$PREFIX"/bin/*.dll | wc -l) DLLs, $(du -sh "$PREFIX/bin" | cut -f1)"
