#!/bin/bash
# After scenario-box.sh and scenario-workplane.sh (a sketch open on a plane
# 45 mm up): a small rectangle there, then a loft from the box's sketch to it.
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" clk 125 115            # Прямоугольник
"$UI" clk 800 450
"$UI" mousemove 900 500; sleep 0.3
"$UI" clk 980 560
"$UI" key Escape Escape Escape   # Принять эскиз: Esc снимает инструмент, выбор, затем выходит
sleep 2
"$UI" clk 296 118            # Лофт: два последних эскиза, «Добавить»
sleep 2
"$UI" mousemove 1300 780
"$UI" shot loft-preview 1.5
"$UI" key Return
sleep 2
"$UI" shot loft 1
