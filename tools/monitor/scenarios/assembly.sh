#!/bin/bash
# tools/monitor scenario: кронштейны.bza (12 components) is opened by the
# monitor; wait for the parts, turn the view, drag components around.
set -e
UI="$(dirname "$0")/../../../scripts/ui-test/ui.sh"
"$UI" start
sleep 10                                   # parts load + solve
"$UI" key f; sleep 1
"$UI" mousemove 800 450; sleep 0.3
"$UI" mousedown 3                          # orbit
for _ in $(seq 30); do "$UI" mousemove_relative -- 8 2; sleep 0.03; done
"$UI" mouseup 3; sleep 0.5
"$UI" key Home; sleep 1
# Drag from a few points: the ones on a component move it.
for p in "620 420" "800 470" "980 520" "700 560"; do
  set -- $p
  "$UI" mousemove "$1" "$2"; sleep 0.3
  "$UI" mousedown 1
  for _ in $(seq 25); do "$UI" mousemove_relative -- 6 3; sleep 0.03; done
  "$UI" mouseup 1; sleep 0.8
done
"$UI" key Escape; sleep 1
