#!/bin/bash
# After scenario-box.sh: a hole, a 3 x 2 rectangular pattern of it, then a
# mirror of the hole about YZ.
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" key h                  # Отверстие
sleep 1.5
"$UI" clk 700 420
"$UI" key Return
sleep 1.5
"$UI" clk 730 94             # Массив прямоугольный (отверстие отмечено само)
sleep 2
"$UI" clk 229 648            # Направление 2: Y
sleep 1.5
"$UI" key Return
sleep 2
"$UI" clk 720 142            # Зеркало (YZ по умолчанию)
sleep 2
"$UI" key Return
sleep 2
"$UI" mousemove 1300 780
"$UI" shot pattern 1.5
