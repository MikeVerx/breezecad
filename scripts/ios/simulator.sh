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
            alive=no
            xcrun simctl spawn "$UDID" launchctl list 2>/dev/null | grep -q "$BUNDLE_ID" && alive=yes
            echo "step $shot: app alive $alive, $(grep -c ERROR "$log" 2>/dev/null || echo 0) errors"
            grep -E "ERROR|WARN|Window:|Viewport|test commands|Crash" "$log" 2>/dev/null | head -20
            [ "$alive" = yes ] && ! grep -q "Crash:\|did not start" "$log" || ok=no
        done
        [ "$ok" = yes ]
        ;;
    *)
        sed -n '2,10p' "$0" >&2
        exit 2
        ;;
esac
