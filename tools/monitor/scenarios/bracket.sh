#!/bin/bash
# tools/monitor scenario: кронштейн.bzc is opened by the monitor; wait for
# the rebuild, turn and zoom the view, fit, home. Window 1440x900 at (0,0).
set -e
UI="$(dirname "$0")/../../../scripts/ui-test/ui.sh"
"$UI" start
sleep 6                                    # open + rebuild
"$UI" mousemove 800 450; sleep 0.3
"$UI" mousedown 3                          # orbit: right drag
for _ in $(seq 40); do "$UI" mousemove_relative -- 7 2; sleep 0.03; done
"$UI" mouseup 3; sleep 0.5
for _ in $(seq 8); do "$UI" click 4; sleep 0.08; done   # wheel zoom in
for _ in $(seq 8); do "$UI" click 5; sleep 0.08; done   # and out
"$UI" key f; sleep 0.5                     # Показать всё
"$UI" key Home; sleep 0.5                  # Домой
"$UI" mousedown 3
for _ in $(seq 40); do "$UI" mousemove_relative -- -5 -3; sleep 0.03; done
"$UI" mouseup 3; sleep 1
