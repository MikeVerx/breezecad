#!/bin/bash
# After scenario-box.sh: a work plane 45 mm above XY, then a sketch on it.
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" clk 838 94             # Плоскость (от XY по умолчанию)
sleep 1.5
"$UI" key ctrl+a; "$UI" type 45
sleep 1
"$UI" mousemove 1300 780
"$UI" shot workplane-preview 1.5
"$UI" key Return             # Готово
sleep 1.5
"$UI" key s                  # Начать эскиз: плоскость выбрана в дереве — эскиз сразу на ней
sleep 2
"$UI" shot workplane-sketch 1
