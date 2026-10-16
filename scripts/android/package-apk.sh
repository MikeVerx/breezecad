#!/usr/bin/env bash
# Packs the Android app into an installable APK: libbreezecad.so and the
# shared libraries it needs (found by DT_NEEDED: OCCT, FreeType, the C++
# runtime), the launcher icon and the example part. No Gradle: aapt2,
# zipalign and apksigner from the Android SDK.
#
# Usage: package-apk.sh <build dir> <deps prefix> <abi> <out.apk>
# Environment:
#   ANDROID_NDK_HOME, ANDROID_HOME (the SDK: build-tools and a platform)
#   BZ_VERSION_CODE      grows with every build, so a new APK installs over
#                        the old one (CI: the run number); default 1
#   BZ_KEYSTORE          signing key (.jks/.p12) and BZ_KEYSTORE_PASSWORD;
#                        the alias is "breezecad". Without them a throwaway
#                        key is made — then each APK needs the old one removed.
set -euo pipefail

BUILD=$(realpath "${1:?build dir}")
DEPS=$(realpath "${2:?deps prefix}")
ABI=${3:?abi}
OUT=$(realpath -m "${4:?out.apk}")
SRC=$(cd "$(dirname "$0")/../.." && pwd)
: "${ANDROID_NDK_HOME:?}" "${ANDROID_HOME:?}"
MIN_SDK=28   # = ANDROID_PLATFORM in CMakePresets.json

NDK_BIN=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin
TOOLS=$(ls -d "$ANDROID_HOME"/build-tools/* | sort -V | tail -1)
PLATFORM=$(ls -d "$ANDROID_HOME"/platforms/android-* | sort -V | tail -1)
TARGET_SDK=${PLATFORM##*-}
case "$ABI" in
    arm64-v8a) triple=aarch64-linux-android ;;
    x86_64)    triple=x86_64-linux-android ;;
    *) echo "unknown ABI $ABI" >&2; exit 1 ;;
esac

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/lib/$ABI" "$W/assets/examples"

# --- Native libraries: libbreezecad.so and what it needs, recursively --------
lookup() {   # a library name → its file among ours (none: a system library)
    for dir in "$BUILD/android" "$DEPS/lib" "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$triple"; do
        [ -f "$dir/$1" ] && { echo "$dir/$1"; return 0; }
    done
    return 0   # not ours: under set -e a non-zero status would end the script
}
queue=(libbreezecad.so)
declare -A seen=()
while [ ${#queue[@]} -gt 0 ]; do
    name=${queue[0]}; queue=("${queue[@]:1}")
    [ -n "${seen[$name]:-}" ] && continue
    seen[$name]=1
    file=$(lookup "$name")
    [ -n "$file" ] || continue   # libc, libm, libEGL…: on the device
    cp "$file" "$W/lib/$ABI/"
    for dep in $("$NDK_BIN/llvm-readelf" -d "$file" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do queue+=("$dep"); done
done
"$NDK_BIN/llvm-strip" --strip-unneeded "$W/lib/$ABI"/*.so

# --- Assets: the example part ----------------------------------------------
cp "$SRC/tests/data/кронштейн.bzc" "$W/assets/examples/bracket.bzc"

# --- Manifest, resources, APK -----------------------------------------------
number=$(sed -n '/^project(/,/)/s/.*VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt" | head -1)
code=${BZ_VERSION_CODE:-1}
sed -e "s/@VERSION_CODE@/$code/" -e "s/@VERSION_NAME@/$number-android.$code/" \
    -e "s/@MIN_SDK@/$MIN_SDK/" -e "s/@TARGET_SDK@/$TARGET_SDK/" \
    "$SRC/resources/android/AndroidManifest.xml.in" > "$W/AndroidManifest.xml"
"$TOOLS/aapt2" compile --dir "$SRC/resources/android/res" -o "$W/res.zip"
"$TOOLS/aapt2" link -o "$W/unsigned.apk" -I "$PLATFORM/android.jar" --manifest "$W/AndroidManifest.xml" \
    -A "$W/assets" "$W/res.zip"
(cd "$W" && zip -qr unsigned.apk lib)
"$TOOLS/zipalign" -f 4 "$W/unsigned.apk" "$W/aligned.apk"

# --- Signature ----------------------------------------------------------------
if [ -n "${BZ_KEYSTORE:-}" ] && [ -s "$BZ_KEYSTORE" ]; then
    keystore=$BZ_KEYSTORE; password=${BZ_KEYSTORE_PASSWORD:?}
else
    echo "warning: no signing key (BZ_KEYSTORE) — a throwaway one; remove the old app before installing this APK" >&2
    keystore=$W/throwaway.jks; password=throwaway
    keytool -genkeypair -keystore "$keystore" -storepass "$password" -keypass "$password" -alias breezecad \
        -keyalg RSA -keysize 2048 -validity 365 -dname "CN=BreezeCAD throwaway" >/dev/null 2>&1
fi
mkdir -p "$(dirname "$OUT")"
BZ_KS_PASS=$password "$TOOLS/apksigner" sign --ks "$keystore" --ks-pass env:BZ_KS_PASS --key-pass env:BZ_KS_PASS \
    --ks-key-alias breezecad --out "$OUT" "$W/aligned.apk"
"$TOOLS/apksigner" verify "$OUT"
echo "APK: $OUT ($(du -h "$OUT" | cut -f1)), version $number-android.$code, target SDK $TARGET_SDK, $(ls "$W/lib/$ABI" | wc -l) libraries"
