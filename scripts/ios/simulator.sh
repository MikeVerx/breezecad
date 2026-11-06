#!/usr/bin/env bash
# The iOS simulator for CI (ios.yml) and for a Mac at hand: boots one iPad
# simulator and runs the unit tests or the app in it.
#
#   simulator.sh tests <build dir> [doctest args]   bz_tests in the simulator
#                                                   (it sees the Mac's files,
#                                                   so BZ_SOURCE_DIR works as is)
#   simulator.sh smoke <BreezeCAD.app> <out dir>    installs and starts the app,
#                                                   waits for the part, takes a
#                                                   screenshot, checks it lives
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
        xcrun simctl launch --terminate-running-process --stdout="$OUT/stdout.log" --stderr="$OUT/app.log" "$UDID" "$BUNDLE_ID"
        # The part is built in a few seconds on a Mac; the first start of a
        # simulator is slower.
        for _ in $(seq 1 90); do
            grep -q "built in\|did not start\|cannot open\|refused" "$OUT/app.log" 2>/dev/null && break
            sleep 1
        done
        sleep 3   # a few frames after the part arrived
        xcrun simctl io "$UDID" screenshot "$OUT/simulator-ios.png" >/dev/null 2>&1 || true
        cat "$OUT/app.log" 2>/dev/null || true
        alive=no
        xcrun simctl spawn "$UDID" launchctl list 2>/dev/null | grep -q "$BUNDLE_ID" && alive=yes
        echo "app alive: $alive"
        grep -q "built in" "$OUT/app.log" && ! grep -q "did not start\|refused" "$OUT/app.log" && [ "$alive" = yes ]
        ;;
    *)
        sed -n '2,10p' "$0" >&2
        exit 2
        ;;
esac
