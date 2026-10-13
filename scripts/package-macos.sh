#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# macOS package: BreezeCAD.app (Apple Silicon) that runs on its own, zipped.
#
#   BreezeCAD.app/Contents/MacOS/breezecad      the program
#   BreezeCAD.app/Contents/Frameworks/*.dylib   OCCT, GLFW, freetype… from
#                                               Homebrew, paths rewritten to
#                                               @executable_path/../Frameworks
#   BreezeCAD.app/Contents/Resources/assets     icons, fonts, splash
#
# Signing: ad hoc by default (arm64 macOS runs no unsigned code at all).
# For a paid "Developer ID" later, set MACOS_SIGN_IDENTITY to the identity
# from the keychain ("Developer ID Application: Name (TEAMID)"): the hardened
# runtime and a timestamp are then added. Notarize the zip after this script:
#   xcrun notarytool submit BreezeCAD-macos.zip --keychain-profile <p> --wait
#   (then staple the .app and zip it again: xcrun stapler staple BreezeCAD.app)
#
# Usage: package-macos.sh <build/bin> <out dir> [--beta]
#   plain: <out>/BreezeCAD-macos.zip      — BreezeCAD.app
#   beta:  <out>/BreezeCAD-beta-macos.zip — BreezeCAD-beta/ with the app (the
#          testers' flag inside, in Contents/Resources) and the instructions.
set -euo pipefail

BIN="${1:?usage: $0 <build/bin> <out dir> [--beta]}"
OUT="${2:?usage: $0 <build/bin> <out dir> [--beta]}"
BETA="${3:-}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
IDENTITY="${MACOS_SIGN_IDENTITY:--}"
SIGN_OPTS=(--force --sign "$IDENTITY")
[ "$IDENTITY" != "-" ] && SIGN_OPTS+=(--options runtime --timestamp)

version=$(sed -n '/^project(/,/)/s/.*VERSION \([0-9.]*\).*/\1/p' "$HERE/CMakeLists.txt" | head -1)
build="${GITHUB_RUN_NUMBER:-0}"

stage=$(mktemp -d)
app="$stage/BreezeCAD.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Frameworks"
cp "$BIN/breezecad" "$app/Contents/MacOS/"
cp -R "$BIN/assets" "$app/Contents/Resources/"
cp "$HERE/assets/brand/breezecad.icns" "$app/Contents/Resources/"
sed -e "s/@VERSION@/${version:-0.0.0}/" -e "s/@BUILD@/$build/" "$HERE/resources/macos/Info.plist.in" > "$app/Contents/Info.plist"
printf 'APPL????' > "$app/Contents/PkgInfo"
plutil -lint "$app/Contents/Info.plist" >/dev/null

# The libraries into Frameworks, every reference rewritten.
prefix="$(brew --prefix)"
dylibbundler -of -cd -b -x "$app/Contents/MacOS/breezecad" -d "$app/Contents/Frameworks" \
    -p @executable_path/../Frameworks/ -s "$prefix/lib" > "$stage/dylibbundler.log"

binaries=("$app/Contents/MacOS/breezecad" "$app"/Contents/Frameworks/*.dylib)
# Search paths into Homebrew are not needed any more and would only hide a miss.
for f in "${binaries[@]}"; do
    otool -l "$f" | awk '/LC_RPATH/ {getline; getline; print $2}' | while read -r rp; do
        case "$rp" in /opt/homebrew/*|/usr/local/*) install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null ;; esac
    done
done
# Nothing may still point outside the bundle and the system.
bad=$(for f in "${binaries[@]}"; do otool -L "$f" | tail -n +2 | awk '{print $1}' \
        | grep -vE '^(/usr/lib/|/System/Library/|@executable_path/\.\./Frameworks/)' | sed "s|^|$(basename "$f"): |"; done || true)
if [ -n "$bad" ]; then
    echo "$bad"
    echo "::error title=package-macos::references outside the bundle: $(echo "$bad" | head -5 | tr '\n' ' ')"
    exit 1
fi
count=$(ls "$app/Contents/Frameworks" | wc -l | tr -d ' ')

if [ "$BETA" = "--beta" ]; then
    cp "$HERE/resources/beta/beta-tester.txt" "$app/Contents/Resources/"
fi

# Sign from the inside out: each library, then the app (which seals the rest).
for f in "$app"/Contents/Frameworks/*.dylib; do codesign "${SIGN_OPTS[@]}" "$f"; done
codesign "${SIGN_OPTS[@]}" "$app"
codesign --verify --deep --strict "$app"
lipo -archs "$app/Contents/MacOS/breezecad" | grep -q arm64

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
if [ "$BETA" = "--beta" ]; then
    beta="$stage/BreezeCAD-beta"
    mkdir -p "$beta"
    mv "$app" "$beta/"
    cp "$HERE/resources/beta/macos/ИНСТРУКЦИЯ для тестеров.txt" "$beta/"
    ditto -c -k --keepParent "$beta" "$OUT/BreezeCAD-beta-macos.zip"
    echo "package-macos: $OUT/BreezeCAD-beta-macos.zip"
else
    echo "::notice title=macOS package::BreezeCAD.app $version ($build): $count libraries inside, $(du -sh "$app" | cut -f1), signed: $IDENTITY"
    ditto -c -k --keepParent "$app" "$OUT/BreezeCAD-macos.zip"
    echo "package-macos: $OUT/BreezeCAD-macos.zip"
fi
rm -rf "$stage"
