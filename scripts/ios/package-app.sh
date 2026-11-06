#!/usr/bin/env bash
# Packs the iOS build into BreezeCAD.app, and for devices into an .ipa
# (Payload/BreezeCAD.app zipped). No Xcode project: the executable from
# CMake, Info.plist from resources/ios, the assets (fonts, icons, languages,
# libraries; read in place: platform::AssetsDir), the example part and the icon.
#
# Usage: package-app.sh <build dir> <sdk> <out dir> [build number]
#   sdk  iphoneos: BreezeCAD.app + BreezeCAD-ios.ipa, ad-hoc signed — to be
#        signed again with an Apple ID (Sideloadly, AltStore) or a paid
#        account for TestFlight
#        iphonesimulator: BreezeCAD.app, ad-hoc signed (simctl install)
set -euo pipefail

BUILD=$(cd "${1:?build dir}" && pwd)
SDK=${2:?sdk: iphoneos or iphonesimulator}
mkdir -p "${3:?out dir}"
OUT=$(cd "$3" && pwd)
NUMBER=${4:-1}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
MIN_OS=16.3   # = CMAKE_OSX_DEPLOYMENT_TARGET of the ios presets

case "$SDK" in
    iphoneos)        platform=iPhoneOS ;;
    iphonesimulator) platform=iPhoneSimulator ;;
    *) echo "unknown SDK $SDK" >&2; exit 1 ;;
esac
version=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$SRC/CMakeLists.txt" | head -1)

APP="$OUT/BreezeCAD.app"
rm -rf "$APP" && mkdir -p "$APP"
cp "$BUILD/ios/breezecad" "$APP/breezecad"
sed -e "s/@VERSION@/$version/" -e "s/@BUILD@/$NUMBER/" -e "s/@PLATFORM@/$platform/" -e "s/@MIN_OS@/$MIN_OS/" \
    "$SRC/resources/ios/Info.plist.in" > "$APP/Info.plist"
plutil -lint "$APP/Info.plist" >/dev/null
printf 'APPL????' > "$APP/PkgInfo"
cp -R "$SRC/assets" "$APP/assets"
mkdir -p "$APP/assets/examples"
cp "$SRC/tests/data/кронштейн.bzc" "$APP/assets/examples/bracket.bzc"   # as in the APK (package-apk.sh)

# The icon: a full-bleed square of its own (iOS rounds the corners itself; the
# macOS .icns has them drawn in and came out cut along the bottom), made from
# assets/brand/breezecad-app-icon-ios.svg. iPhone 60 pt @2x/@3x, iPad 76 and 83.5 pt @2x.
icon="$SRC/assets/brand/breezecad-app-icon-ios.png"
for spec in AppIcon60x60@2x:120 AppIcon60x60@3x:180 AppIcon76x76@2x~ipad:152 AppIcon83.5x83.5@2x~ipad:167; do
    sips -z "${spec##*:}" "${spec##*:}" "$icon" --out "$APP/${spec%%:*}.png" >/dev/null
done

codesign --force --sign - --timestamp=none "$APP"
echo "BreezeCAD.app $version ($NUMBER) for $platform: $(du -sh "$APP" | cut -f1)"

if [ "$SDK" = iphoneos ]; then
    stage=$(mktemp -d)
    mkdir "$stage/Payload" && cp -R "$APP" "$stage/Payload/"
    rm -f "$OUT/BreezeCAD-ios.ipa"
    (cd "$stage" && zip -qry "$OUT/BreezeCAD-ios.ipa" Payload)
    echo "BreezeCAD-ios.ipa: $(du -h "$OUT/BreezeCAD-ios.ipa" | cut -f1)"
fi
