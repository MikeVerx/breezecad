#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Linux package: a folder that runs on its own, packed as tar.gz.
#
#   BreezeCAD/breezecad     the program (RUNPATH $ORIGIN/lib)
#   BreezeCAD/assets/       icons, fonts, splash
#   BreezeCAD/lib/          every library it needs that a desktop may lack
#
# Built with conda-forge (GCC 13, OCCT, GLFW): its libraries need glibc 2.28
# at most, so the package runs on any distribution from 2019 on. What stays
# with the system: glibc itself, the OpenGL driver stack and X11 (they must
# match the machine), and fontconfig (its configuration is the system's).
#
# Usage: package-linux.sh <build/bin> <out dir> [--beta]
#   plain: <out>/BreezeCAD-linux.tar.gz
#   beta:  <out>/BreezeCAD-beta-linux.tar.gz — BreezeCAD-beta/ with the
#          launcher, the instructions and program/ with the testers' flag.
set -euo pipefail

BIN="${1:?usage: $0 <build/bin> <out dir> [--beta]}"
OUT="${2:?usage: $0 <build/bin> <out dir> [--beta]}"
BETA="${3:-}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"

# Libraries every Linux desktop has and that must be the system's own.
is_system() {
    case "$1" in
        linux-vdso.so*|ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|libutil.so*) return 0 ;;
        libGL.so*|libGLX*|libGLdispatch*|libOpenGL*|libEGL*|libGLU*|libdrm*|libgbm*|libwayland-*) return 0 ;;
        libX11*|libxcb*|libXext*|libXau*|libXdmcp*|libXfixes*|libXrender*|libXrandr*|libXinerama*|libXcursor*|libXi.so*|libXxf86vm*) return 0 ;;
        libfontconfig*) return 0 ;;
    esac
    return 1
}

stage=$(mktemp -d)
app="$stage/BreezeCAD"
mkdir -p "$app/lib"
cp "$BIN/breezecad" "$app/"
cp -r "$BIN/assets" "$app/"

# Every library ldd resolves, recursively (ldd already walks the tree).
copied=0
while read -r name path; do
    is_system "$name" && continue
    [ -f "$path" ] || { echo "::error title=package-linux::not found: $name"; exit 1; }
    cp -L "$path" "$app/lib/$name"
    copied=$((copied + 1))
done < <(ldd "$app/breezecad" | awk '/=>/ && $3 ~ /^\// {print $1, $3}')
echo "package-linux: $copied libraries bundled"

patchelf --set-rpath '$ORIGIN/lib' "$app/breezecad"
for lib in "$app"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$lib"; done
strip --strip-unneeded "$app/breezecad" "$app"/lib/*.so* 2>/dev/null || true

# Nothing may still point into the build environment.
if ldd "$app/breezecad" | grep -E "conda|micromamba|not found"; then
    echo "::error title=package-linux::a library resolves outside the package (above)"
    exit 1
fi
glibc=$( (objdump -T "$app/breezecad" "$app"/lib/*.so* 2>/dev/null || true) | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "::notice title=Linux package::needs ${glibc:-?} at most; $copied libraries inside; $(du -sh "$app" | cut -f1) unpacked"

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
if [ "$BETA" = "--beta" ]; then
    beta="$stage/BreezeCAD-beta"
    mkdir -p "$beta"
    mv "$app" "$beta/program"
    cp "$HERE/resources/beta/beta-tester.txt" "$beta/program/"
    cp "$HERE/resources/beta/linux/Запустить BreezeCAD.sh" "$HERE/resources/beta/linux/ИНСТРУКЦИЯ для тестеров.txt" "$beta/"
    chmod +x "$beta/Запустить BreezeCAD.sh" "$beta/program/breezecad"
    tar -C "$stage" -czf "$OUT/BreezeCAD-beta-linux.tar.gz" BreezeCAD-beta
    echo "package-linux: $OUT/BreezeCAD-beta-linux.tar.gz"
else
    chmod +x "$app/breezecad"
    tar -C "$stage" -czf "$OUT/BreezeCAD-linux.tar.gz" BreezeCAD
    echo "package-linux: $OUT/BreezeCAD-linux.tar.gz"
fi
rm -rf "$stage"
