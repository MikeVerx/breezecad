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
# Its file types' icons (Info.plist: CFBundleDocumentTypes), from the same
# pictures as Windows' and Linux' (scripts/installer/make-icons.py).
for kind in part assembly project module library; do
    set_dir="$stage/$kind.iconset"
    mkdir -p "$set_dir"
    for size in 16 32 128 256; do
        sips -z $size $size "$HERE/installer/icons/png/$kind.png" --out "$set_dir/icon_${size}x${size}.png" >/dev/null
        double=$((size * 2)); [ $double -le 256 ] && sips -z $double $double "$HERE/installer/icons/png/$kind.png" --out "$set_dir/icon_${size}x${size}@2x.png" >/dev/null
    done
    iconutil -c icns "$set_dir" -o "$app/Contents/Resources/$kind.icns"
done
# Finder's pictures of parts, assemblies and projects: the Quick Look extension.
if [ -f "$BIN/BreezeCADThumbnails" ]; then
    appex="$app/Contents/PlugIns/BreezeCADThumbnails.appex"
    mkdir -p "$appex/Contents/MacOS"
    cp "$BIN/BreezeCADThumbnails" "$appex/Contents/MacOS/"
    sed -e "s/@VERSION@/${version:-0.0.0}/" -e "s/@BUILD@/$build/" "$HERE/installer/macos/Info.plist.in" > "$appex/Contents/Info.plist"
    plutil -lint "$appex/Contents/Info.plist" >/dev/null
fi
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
# Each search path once: dylibbundler adds @executable_path/../Frameworks/ to
# libraries that already have it, and the dyld of macOS 15 refuses to load a
# library with a repeated LC_RPATH ("duplicate LC_RPATH") — the app did not start.
rpaths() { otool -l "$1" | awk '/LC_RPATH/ {getline; getline; print $2}'; }
for f in "${binaries[@]}"; do
    list=$(rpaths "$f")
    for rp in $(printf '%s\n' "$list" | sort | uniq -d); do
        # delete_rpath removes one entry (or, in some versions, all): delete
        # until none is left, then put it back once.
        while printf '%s\n' "$(rpaths "$f")" | grep -qxF "$rp"; do
            install_name_tool -delete_rpath "$rp" "$f"
        done
        install_name_tool -add_rpath "$rp" "$f"
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
# The extension first (signed inside out), sandboxed as app extensions must be.
[ -d "$app/Contents/PlugIns/BreezeCADThumbnails.appex" ] &&
    codesign "${SIGN_OPTS[@]}" --entitlements "$HERE/installer/macos/Thumbnails.entitlements" "$app/Contents/PlugIns/BreezeCADThumbnails.appex"
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
    # The disk image: the app and a link to Applications on a picture that
    # says to drag one onto the other (the window's layout is Finder's: set
    # by AppleScript when a Finder is there to ask, plain otherwise).
    dmg="$stage/dmg"
    mkdir -p "$dmg/.background"
    cp -R "$app" "$dmg/"
    ln -s /Applications "$dmg/Applications"
    cp "$HERE/installer/macos/dmg-background.png" "$dmg/.background/background.png"
    rw="$stage/rw.dmg"
    hdiutil create -quiet -volname "BreezeCAD" -srcfolder "$dmg" -fs HFS+ -format UDRW -ov "$rw"
    device=$(hdiutil attach -readwrite -noverify -noautoopen "$rw" | awk '/\/Volumes\// {print $1; exit}')
    osascript <<'APPLESCRIPT' || echo "::warning title=package-macos::the disk image window layout was not set (no Finder)"
tell application "Finder"
    tell disk "BreezeCAD"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 840, 520}
        set opts to the icon view options of container window
        set arrangement of opts to not arranged
        set icon size of opts to 112
        set background picture of opts to file ".background:background.png"
        set position of item "BreezeCAD.app" of container window to {160, 190}
        set position of item "Applications" of container window to {480, 190}
        update without registering applications
        close
    end tell
end tell
APPLESCRIPT
    sync
    hdiutil detach -quiet "$device" || hdiutil detach -force -quiet "$device"
    hdiutil convert -quiet "$rw" -format UDZO -imagekey zlib-level=9 -o "$OUT/BreezeCAD-macos.dmg"
    codesign "${SIGN_OPTS[@]}" "$OUT/BreezeCAD-macos.dmg" || true
    echo "package-macos: $OUT/BreezeCAD-macos.dmg ($(du -h "$OUT/BreezeCAD-macos.dmg" | cut -f1))"
fi
rm -rf "$stage"
