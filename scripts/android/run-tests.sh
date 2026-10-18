#!/usr/bin/env bash
# Runs the unit tests on an Android device or emulator through adb.
# CI runs it on an x86_64 emulator; on a phone (arm64) the same way:
# switch on USB debugging, connect, then from the unpacked artifact
# "bz-tests-android-arm64":   bash run-tests.sh .
#
# Usage: run-tests.sh <staged folder> [extra bz_tests arguments]
# The folder is what stage-tests.sh made. Exit status is the tests' own.
set -euo pipefail

STAGE=$(realpath "${1:?staged folder (stage-tests.sh)}")
shift
DEV=/data/local/tmp/breezecad   # = BZ_TEST_DEVICE_DIR in tests/CMakeLists.txt

adb wait-for-device
echo "device: $(adb shell getprop ro.product.model | tr -d '\r'), Android $(adb shell getprop ro.build.version.release | tr -d '\r'), $(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
adb shell rm -rf "$DEV"
adb shell mkdir -p "$DEV/tmp" "$DEV/home"
# One push per folder: adb copies a folder with its contents.
adb push -q "$STAGE/bz_tests" "$DEV/"
for d in lib modules source; do adb push -q "$STAGE/$d" "$DEV/"; done
adb shell chmod 755 "$DEV/bz_tests"

# adb shell passes the exit status through (Android 7+).
set +e
adb shell "cd $DEV && TMPDIR=$DEV/tmp HOME=$DEV/home LD_LIBRARY_PATH=$DEV/lib ./bz_tests $*"
status=$?
set -e
echo "bz_tests exit status: $status"
exit $status
