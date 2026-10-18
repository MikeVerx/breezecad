#!/usr/bin/env bash
# Starts the APK on a device or emulator and checks that the program lives
# through its main paths, taking screenshots of each (they go to the
# pre-release android-build next to the APK):
#
#   1-home           the home page (phone layout: projects folder, new part)
#   2-part           the example part open: blocks 1–5, the strip, the cube button
#   3-turned         after a one-finger swipe (the view turns)
#   4-sketch         a sketch with the line tool: the drop cursor after a tap
#   5-sketch-line    after "OK" twice: a line drawn by the cursor
#   6-lefty-icons    left-handed, block 2 as icons only
#   7-tablet         the desktop layout forced (as on a tablet)
#   8-cube           the big view cube open
#
# The app is told what to do by the intent's "bz" extra (Application::RunTestCommands).
# A step that kills the app fails the run; its log and the system's errors are kept.
#
# Usage: smoke-apk.sh <app.apk> <out dir>
set -uo pipefail

APK=$(realpath "${1:?apk}")
OUT=$(realpath -m "${2:?out dir}")
PACKAGE=io.github.mikeverx.breezecad
ACTIVITY=$PACKAGE/.BreezeActivity
mkdir -p "$OUT"
failed=0

adb wait-for-device
adb install -r "$APK" || { echo "install failed"; exit 1; }
# The watchdog's notice permission (Android 13+) would cover the screenshots with a dialog.
adb shell pm grant $PACKAGE android.permission.POST_NOTIFICATIONS 2>/dev/null || true
adb logcat -c

size=$(adb shell wm size | sed -n 's/.*: \([0-9]*x[0-9]*\).*/\1/p' | tail -1)
w=${size%x*}; h=${size#*x}
[ "$w" -lt "$h" ] && { t=$w; w=$h; h=$t; }   # the app is landscape
density=$(adb shell wm density | sed -n 's/.*: \([0-9]*\).*/\1/p' | tail -1)
dp() { echo $(( $1 * density / 160 )); }      # dp → pixels
echo "screen ${w}x${h}, density $density"
# Blocks 1 and 2 (mobile/MobileLayout.cpp): 20 % of the width, at least 170 and 150 dp.
wdp=$(( w * 160 / density ))
b1=$(( wdp / 5 > 170 ? wdp / 5 : 170 )); b2=$(( wdp / 5 > 150 ? wdp / 5 : 150 ))
b1=$(dp $b1); b2=$(dp $b2)

alive() { adb shell pidof $PACKAGE | tr -d '\r'; }
shot() { sleep "${2:-2}"; adb exec-out screencap -p > "$OUT/$1.png"; }
start() {   # start <commands>: a fresh process with them
    adb shell am force-stop $PACKAGE
    adb shell am start -W -n $ACTIVITY ${1:+--es bz "$1"} >/dev/null
    # The first start unpacks the assets; a part is built on a thread (the
    # emulator draws in software): give it time, as long as it lives.
    for _ in $(seq 1 ${2:-20}); do
        sleep 1
        [ -n "$(alive)" ] || break
    done
}
check() {   # check <step>: the process still there
    if [ -z "$(alive)" ]; then
        echo "step $1: the app is gone"
        failed=1
    else
        echo "step $1: alive"
    fi
}

# 1. The home page.
start "" 25
shot 1-home
check home

# 2–3. The example part, then a one-finger swipe across the model (it turns).
start example 30
shot 2-part
check part
adb shell input swipe $((w / 2)) $((h / 2)) $((w / 2 + w / 8)) $((h / 2 + h / 10)) 400
shot 3-turned
check turned

# 4–5. A sketch with the line tool: a tap moves the cursor, OK sets a point;
# a second place and OK — a line. OK is the row's last button of block 1
# (left of the model); its place is found from the layout's proportions.
start sketch,line 25
cx=$((w / 2)); cy=$((h / 2))
adb shell input tap $cx $cy
shot 4-sketch 2
check sketch
ok_x=$(( b1 - $(dp 30) ))               # OK: block 1's row, nearest the model
ok_y=$(( h - $(dp 25) ))                # the row is block 1's bottom (18.10), its middle
adb shell input tap $ok_x $ok_y
adb shell input swipe $cx $cy $((cx + w / 10)) $((cy - h / 8)) 500
adb shell input tap $ok_x $ok_y
shot 5-sketch-line 2
check sketch-line

# 6. Left hand, block 2 as icons.
start example,lefty,icons 25
shot 6-lefty-icons
check lefty

# 7. The desktop layout, as on a tablet.
start example,desktop 25
shot 7-tablet
check tablet

# 8. The big cube: its button at the model's top right.
start example 25
right=$(( w - b2 ))
adb shell input tap $((right - $(dp 30))) $(dp 30)
shot 8-cube
check cube
# A face of the big cube — the view turns to it. The app logs where the
# panel is; the cube's middle is the panel's middle (ui/ViewCube.cpp), the
# front face a little left of it and down.
adb logcat -d -s BreezeCAD:* | tr -d '\r' > "$OUT/cube.log"
read -r px py ps < <(sed -n 's/.*Touch: cube panel at \([0-9]*\),\([0-9]*\) size \([0-9]*\).*/\1 \2 \3/p' "$OUT/cube.log" | tail -1)
if [ -n "${ps:-}" ]; then
    cs=$(( ps * 33 / 172 ))   # the cube's half edge (dp)
    adb shell input tap $(dp $(( px + ps / 2 - cs * 6 / 10 ))) $(dp $(( py + ps / 2 + cs / 2 )))
else
    echo "step cube-face: the cube panel's place is not in the log"
fi
shot 9-cube-face
check cube-face

# 10–12. Compact block 2 with a group's strip open; the settings on the phone;
# the layout editor (the author's mockups, 17.10.2026).
start example,strip 25
shot 10-tool-strip
check tool-strip
start example,settings 25
shot 11-settings
check settings
start example,layout 25
shot 12-layout-editor
check layout-editor

# 13–14. Real taps, as a finger does them (17.10: the author's «›» and the
# extrude field did nothing, while the test commands above opened them fine).
# The app logs where the first fold opener and the operation's fields are, in dp.
broken=0
adb logcat -d -s BreezeCAD:* > "$OUT/breezecad-1.log"   # steps 1–12: the log is cleared below
blog() { adb logcat -d -s BreezeCAD:* | tr -d '\r'; }

# 13. A folding group's opener in block 2 (18.10: «Изменить» of the 3D tab) tapped: the group opens.
adb logcat -c
start example 25
read -r mx my ms < <(blog | sed -n 's/.*Touch: fold [^ ]* at \([0-9-]*\),\([0-9-]*\) size \([0-9]*\).*/\1 \2 \3/p' | head -1)
if [ -n "${ms:-}" ]; then
    adb shell input tap $(dp $(( mx + ms / 2 ))) $(dp $(( my + ms / 2 )))
    shot 13-fold-tapped 3
    if blog | grep -q "Fold: open"; then
        echo "step fold-tap: the group opened ($(blog | grep -o 'Fold: .*' | tr '\n' ';'))"
    else
        echo "step fold-tap: FAILED — the group did not open ($(blog | grep -o 'Fold: .*' | tr '\n' ';'))"; broken=1
    fi
    # Tapped again: it folds back (and stays folded for the next steps).
    adb shell input tap $(dp $(( mx + ms / 2 ))) $(dp $(( my + ms / 2 )))
    sleep 2
    blog | grep -q "Fold: closed" && echo "step fold-tap: folded back" || { echo "step fold-tap: FAILED — did not fold back"; broken=1; }
else
    echo "step fold-tap: FAILED — no opener in the log"; broken=1
fi
check fold-tap

# 14. Extrude (a new part, an empty sketch: the example has no unused sketch): the field loses the focus it opens with (a tap on the model),
# then a tap on the field opens the numpad, and it is still open 3 s later.
adb logcat -c
start touchkeys,new,sketch,extrude 30
fieldat() { blog | sed -n 's/.*Touch: field distance at \([0-9-]*\),\([0-9-]*\) size \([0-9]*\)x\([0-9]*\).*/\1 \2 \3 \4/p' | tail -1; }
adb shell input tap $((w / 2)) $((h / 3))
sleep 2
read -r fx fy fw fh < <(fieldat)   # where it is now, the layout settled
if [ -n "${fh:-}" ]; then
    adb shell input tap $(dp $(( fx + fw / 3 ))) $(dp $(( fy + fh / 2 )))
    shot 14-extrude-numpad 3
    seq=$(blog | grep -o 'Keyboard: [a-z]* [a-z]*' | tr '\n' ';')
    if [ "$(blog | grep -o 'Keyboard: typing [a-z]*' | tail -1)" = "Keyboard: typing on" ] &&
       [ "$(blog | grep -o 'Keyboard: numpad [a-z]*' | tail -1)" = "Keyboard: numpad on" ]; then
        echo "step extrude-field: the numpad is open (field at $fx,$fy; $seq)"
    else
        echo "step extrude-field: FAILED — the numpad is not open (field at $fx,$fy; $seq)"; broken=1
    fi
else
    echo "step extrude-field: FAILED — no field in the log"; broken=1
fi
check extrude-field

# 14b. The extrude's choices are in the top island (18.10): a tap on one sets it (the log says so).
# On the example part: there is a body, so every operation can be chosen.
adb logcat -c
start touchkeys,example,extrude 30
read -r cid cix cx0 cy0 cs0 < <(blog | sed -n 's/.*Touch: choice \([a-z-]*\) \([0-9]*\) at \([0-9-]*\),\([0-9-]*\) size \([0-9]*\).*/\1 \2 \3 \4 \5/p' | head -1)
if [ -n "${cs0:-}" ]; then
    adb shell input tap $(dp $(( cx0 + cs0 / 2 ))) $(dp $(( cy0 + 15 )))
    shot 14b-choice-tapped 3
    if blog | grep -q "Choice: $cid $cix"; then
        echo "step choice-tap: $cid set to $cix"
    else
        echo "step choice-tap: FAILED — no «Choice: $cid $cix» ($(blog | grep -o 'Choice: .*' | tr '\n' ';'))"; broken=1
    fi
else
    echo "step choice-tap: FAILED — no choice island in the log"; broken=1
fi
check choice-tap

adb logcat -d -s BreezeCAD:* > "$OUT/breezecad.log"
adb logcat -d '*:E' > "$OUT/errors.log"
grep -E "OpenGL ES|Window:|ERROR|WARN|test commands" "$OUT/breezecad.log" | head -20
echo "touch: $(tr -d '\r' < "$OUT/breezecad.log" | grep -o 'Touch: .*' | tail -12 | tr '\n' ';')"

# 15. "Back" on the home page (18.10: an edge swipe closed the app): the first only
# says "once more", the app stays in front; the second leaves. Then started again.
focused() { adb shell dumpsys window | grep -E 'mCurrentFocus|mFocusedApp' | grep -c "$PACKAGE"; }
adb logcat -c
start home 15   # "home": a test run — no first-start setup over the home page
adb shell input keyevent KEYCODE_BACK
sleep 2
shot 15-back-once 1
if [ "$(focused)" -gt 0 ] && blog | grep -q "Back: once more"; then
    echo "step back-once: stayed, asked for a second"
else
    echo "step back-once: FAILED — focus $(focused), log: $(blog | grep -o 'Back: .*' | tr '\n' ';')"; broken=1
fi
adb shell input keyevent KEYCODE_BACK
sleep 3
if [ "$(focused)" -eq 0 ]; then
    echo "step back-twice: left"
else
    echo "step back-twice: FAILED — still in front"; broken=1
fi
start "" 15
if [ $failed -ne 0 ] || [ -z "$(alive)" ]; then
    echo "the app died:"; grep -E "FATAL|DEBUG|libbreezecad|backtrace|#[0-9]+ pc|AndroidRuntime" "$OUT/errors.log" | head -60
    exit 1
fi
if [ $broken -ne 0 ]; then
    echo "a tap check failed (steps 13–15 above)"
    exit 1
fi
echo "smoke test passed"
