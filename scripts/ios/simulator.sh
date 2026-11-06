#!/usr/bin/env bash
# The iOS simulator for CI (ios.yml) and for a Mac at hand: boots one iPad
# simulator and runs the unit tests or the app in it.
#
#   simulator.sh tests <build dir> [doctest args]   bz_tests in the simulator
#                                                   (it sees the Mac's files,
#                                                   so BZ_SOURCE_DIR works as is)
#   simulator.sh smoke <BreezeCAD.app> <out dir>    installs the app and starts
#                                                   it three times (home page,
#                                                   the example, a sketch): log,
#                                                   screenshot, still alive
set -euo pipefail

BUNDLE_ID=io.github.mikeverx.breezecad

# The newest iPad of the newest iOS runtime installed (any iPhone if none).
pick() {
    xcrun simctl list devices available -j | python3 -c '
import json, sys
devices = json.load(sys.stdin)["devices"]
runtimes = sorted((r for r in devices if "iOS" in r), key=lambda r: [int(x) for x in r.rsplit("iOS-", 1)[-1].split("-") if x.isdigit()])
for kind in ("iPad", "iPhone"):
    for r in reversed(runtimes):
        for d in devices[r]:
            if d["name"].startswith(kind):
                print(d["udid"], "|", d["name"], "|", r.rsplit(".", 1)[-1]); sys.exit(0)
sys.exit("no iOS simulator installed")'
}

boot() {
    local line
    line=$(pick)
    UDID=${line%% |*}
    echo "simulator: ${line#* | }" >&2
    xcrun simctl boot "$UDID" 2>/dev/null || true   # already booted is fine
    xcrun simctl bootstatus "$UDID" -b >/dev/null
}

case "${1:-}" in
    tests)
        BUILD=$(cd "${2:?build dir}" && pwd)
        shift 2
        boot
        set +e
        xcrun simctl spawn "$UDID" "$BUILD/bin/bz_tests" "$@"
        status=$?
        set -e
        echo "bz_tests exit status: $status"
        exit $status
        ;;
    smoke)
        APP=$(cd "${2:?BreezeCAD.app}" && pwd)
        mkdir -p "${3:?out dir}"
        OUT=$(cd "$3" && pwd)
        boot
        xcrun simctl uninstall "$UDID" "$BUNDLE_ID" 2>/dev/null || true
        xcrun simctl install "$UDID" "$APP"
        ok=yes
        # Each start: test commands (Application_mobile.cpp, RunTestCommands;
        # "home" — the home page as it opens, no first-start setup over it),
        # the log, a screenshot, and whether the app still lives.
        for run in home:home example:example example,sketch:sketch; do
            commands=${run%%:*}
            shot=${run##*:}
            log="$OUT/app-$shot.log"
            SIMCTL_CHILD_BZ_TEST_COMMANDS="$commands" \
                xcrun simctl launch --terminate-running-process --stdout="$OUT/stdout-$shot.log" --stderr="$log" "$UDID" "$BUNDLE_ID" >/dev/null
            for _ in $(seq 1 60); do
                grep -q "test commands\|Crash:\|did not start" "$log" 2>/dev/null && break
                sleep 1
            done
            [ "$commands" = home ] || for _ in $(seq 1 30); do
                grep -q "Rebuild:\|built in" "$log" 2>/dev/null && break
                sleep 1
            done
            sleep 4   # a few frames after
            xcrun simctl io "$UDID" screenshot "$OUT/simulator-ios-$shot.png" >/dev/null 2>&1 || true
            # A small copy of the picture in the log itself: a pull request
            # publishes no release, and its artifacts need a login to fetch.
            if [ -f "$OUT/simulator-ios-$shot.png" ]; then
                sips -Z 900 -s format jpeg -s formatOptions 60 "$OUT/simulator-ios-$shot.png" --out "$OUT/small-$shot.jpg" >/dev/null 2>&1 &&
                    { echo "SHOT-BEGIN $shot"; base64 -i "$OUT/small-$shot.jpg" | fold -w 4000; echo "SHOT-END $shot"; }
            fi
            # The list first, then the search: "| grep -q" stops reading at the
            # first match, launchctl dies of SIGPIPE, and with pipefail a live
            # app read as dead (the sketch step "failed" so, 2026-11-06).
            alive=no
            running=$(xcrun simctl spawn "$UDID" launchctl list 2>/dev/null || true)
            case "$running" in *"$BUNDLE_ID"*) alive=yes ;; esac
            echo "step $shot: app alive $alive, $(grep -c ERROR "$log" 2>/dev/null || echo 0) errors"
            grep -E "ERROR|WARN|Window:|Viewport|test commands|Crash" "$log" 2>/dev/null | head -20
            if [ "$alive" != yes ] || grep -q "Crash:\|did not start" "$log"; then
                ok=no
                echo "== the end of the log of step $shot"
                tail -25 "$log"
            fi
        done
        if [ "$ok" != yes ]; then
            # Why it died: the program's own report (crash::ReportPath, in its
            # data) and the system's crash log of the simulator's process.
            data=$(xcrun simctl get_app_container "$UDID" "$BUNDLE_ID" data 2>/dev/null || true)
            for r in "$data/Library/Application Support/reports/"crash-*.txt "$data/Library/Application Support/reports/"hang-*.txt; do
                [ -f "$r" ] && { echo "== report $(basename "$r")"; head -60 "$r"; }
            done
            sleep 15   # the system writes its crash log a little later
            ls -lt "$HOME/Library/Logs/DiagnosticReports/" 2>/dev/null | head -8
            ips=$(ls -t "$HOME/Library/Logs/DiagnosticReports/"*.ips 2>/dev/null | head -1)
            [ -n "$ips" ] && { echo "== system crash log $(basename "$ips")"; python3 -c '
import json, sys
text = open(sys.argv[1]).read()
head, _, body = text.partition("\n")
d = json.loads(body)
print("exception:", d.get("exception"), "termination:", d.get("termination", {}).get("indicator"))
images = d.get("usedImages", [])
for t in d.get("threads", []):
    if not t.get("triggered"): continue
    for f in t.get("frames", [])[:40]:
        img = images[f["imageIndex"]]["name"] if f.get("imageIndex") is not None and f["imageIndex"] < len(images) else "?"
        print(" ", img, f.get("symbol", hex(f.get("imageOffset", 0))), "+", f.get("symbolLocation", ""))
' "$ips" 2>&1 | head -60; }
        fi
        [ "$ok" = yes ]
        ;;
    *)
        sed -n '2,10p' "$0" >&2
        exit 2
        ;;
esac
