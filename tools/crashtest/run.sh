#!/bin/bash
# Crash test of a BreezeCAD build on a virtual display (Linux): the safety
# net, random command chains, damaged files, a monkey clicking everywhere.
# Every "Recovered:" line in the log is a failure the safety net caught —
# a bug to fix even though nothing crashed.
#
#   tools/crashtest/run.sh [BIN_DIR] [STEPS]
#   BZ_BIN=build/linux-asan/bin tools/crashtest/run.sh     (AddressSanitizer)
#
# Needs: Xvfb, xdotool, python3 (scripts/ui-test/ui.sh), the mcp module.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BIN="${1:-${BZ_BIN:-$ROOT/build/linux-debug/bin}}"
export BZ_BIN="$(cd "$BIN" && pwd)"   # absolute: ui.sh starts the program from its folder
STEPS="${2:-300}"
export BZ_FAULT_INJECTION=1 ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
export BZ_CRASHTEST="${BZ_CRASHTEST:-/tmp/bz-crashtest}"
LOG=/tmp/breezecad-ui.log
UI="$ROOT/scripts/ui-test/ui.sh"
alive() { pgrep -x breezecad >/dev/null; }
# Started, and its module answering (the connection file is rewritten at each start).
restart() {
  "$UI" start >/dev/null
  for _ in $(seq 60); do
    python3 -c "from bz import call; call('app.documents')" 2>/dev/null && return 0
    sleep 0.5
  done
  echo "the program does not answer"; exit 2
}
cd "$HERE"
restart
python3 samples.py "$BZ_CRASHTEST/samples" || exit 2
status=0
python3 recovery.py || status=1
for seed in 1 2 3; do
  alive || { echo "DIED before fuzz $seed"; status=1; restart; }
  timeout 3000 python3 fuzz.py "$seed" "$STEPS" | tail -1
done
alive || { echo "DIED"; status=1; restart; }
timeout 3000 python3 filefuzz.py 1 60 | tail -1
for seed in 1 2; do
  alive || { echo "DIED before monkey $seed"; status=1; restart; }
  timeout 3000 python3 monkey.py "$seed" "$STEPS" | tail -1
done
alive || { echo "DIED"; status=1; }
rec=$(grep -a -c "Recovered:" "$LOG")
san=$(grep -a -c "AddressSanitizer\|runtime error" "$LOG")
echo "safety net recoveries: $rec (the debug.fault ones of recovery.py: 12); sanitizer reports: $san"
[ "$san" -eq 0 ] || status=1
"$UI" stop
exit $status
