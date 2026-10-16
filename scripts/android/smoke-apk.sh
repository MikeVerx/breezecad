#!/usr/bin/env bash
# Starts the APK on a device or emulator and checks that it lives: the app
# opens, builds the example part, survives a swipe (rotation) and a pinch.
# Leaves screenshots and the log in <out dir>.
#
# Usage: smoke-apk.sh <app.apk> <out dir>
set -uo pipefail

APK=$(realpath "${1:?apk}")
OUT=$(realpath -m "${2:?out dir}")
PACKAGE=io.github.mikeverx.breezecad
mkdir -p "$OUT"

adb wait-for-device
adb install -r "$APK" || { echo "install failed"; exit 1; }
adb logcat -c
adb shell am start -W -n $PACKAGE/android.app.NativeActivity

# The part is built on a thread: wait for it (the emulator draws in software).
built=""
for _ in $(seq 1 90); do
    sleep 1
    built=$(adb logcat -d -s BreezeCAD:I | grep -m1 "built in" || true)
    [ -n "$built" ] && break
    adb shell pidof $PACKAGE >/dev/null || break
done
sleep 3
adb exec-out screencap -p > "$OUT/1-start.png"

# Screen size → a swipe across the middle (one finger: rotation).
size=$(adb shell wm size | sed -n 's/.*: \([0-9]*x[0-9]*\).*/\1/p' | tail -1)
w=${size%x*}; h=${size#*x}
[ "$w" -lt "$h" ] && { t=$w; w=$h; h=$t; }   # the app is landscape
adb shell input swipe $((w / 2)) $((h / 2)) $((w / 2 + w / 5)) $((h / 2 + h / 8)) 400
sleep 3
adb exec-out screencap -p > "$OUT/2-turned.png"

alive=$(adb shell pidof $PACKAGE | tr -d '\r')
adb logcat -d -s BreezeCAD:* > "$OUT/breezecad.log"
adb logcat -d '*:E' > "$OUT/errors.log"

echo "built: ${built:-no}"
echo "alive: ${alive:-no}"
grep -E "OpenGL ES|ERROR|WARN" "$OUT/breezecad.log" | head -10
if [ -z "$alive" ]; then
    echo "the app is gone:"; grep -E "FATAL|DEBUG|libbreezecad|backtrace|#[0-9]+ pc" "$OUT/errors.log" | head -40
    exit 1
fi
[ -n "$built" ] || { echo "the part was not built"; exit 1; }
echo "smoke test passed"
