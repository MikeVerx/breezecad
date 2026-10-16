#!/usr/bin/env bash
# Gathers everything bz_tests needs on a device into one folder: the test
# program, the example modules, the shared libraries (OCCT, FreeType, the C++
# runtime) and the data the tests read from the sources (assets, src, tests).
# CI keeps the folder as an artifact; run-tests.sh (copied in) runs it.
#
# Usage: stage-tests.sh <build dir> <deps prefix> <abi> <out dir>
set -euo pipefail

BUILD=$(realpath "${1:?build dir}")
DEPS=$(realpath "${2:?deps prefix}")
ABI=${3:?abi}
OUT=$(realpath -m "${4:?out dir}")
SRC=$(cd "$(dirname "$0")/../.." && pwd)
: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to the NDK folder}"

case "$ABI" in
    arm64-v8a) triple=aarch64-linux-android ;;
    x86_64)    triple=x86_64-linux-android ;;
    *) echo "unknown ABI $ABI" >&2; exit 1 ;;
esac

rm -rf "$OUT" && mkdir -p "$OUT/lib" "$OUT/source"
cp "$BUILD/bin/bz_tests" "$OUT/"
cp -r "$BUILD/modules" "$OUT/"
cp "$DEPS"/lib/*.so "$OUT/lib/"
cp "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$triple/libc++_shared.so "$OUT/lib/"
# The paths the tests know (tests/CMakeLists.txt, BZ_SOURCE_DIR on Android).
for d in assets src tests translations resources; do cp -r "$SRC/$d" "$OUT/source/"; done
# The NDK builds with -g even in Release: strip the copies (the build keeps
# its own): less to download and to push to a phone.
"$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip --strip-unneeded "$OUT/bz_tests" "$OUT"/lib/*.so
cp "$SRC/scripts/android/run-tests.sh" "$OUT/"
echo "$ABI" > "$OUT/abi.txt"
echo "staged: $OUT ($(du -sh "$OUT" | cut -f1), $(ls "$OUT/lib" | wc -l) libraries)"
