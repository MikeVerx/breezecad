#!/bin/bash
# After scenario-box.sh: round two top edges of the box with R8.
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" clk 458 115            # Сопряжение
sleep 1.5
"$UI" clk 750 527            # верхнее переднее ребро
"$UI" clk 1105 568           # верхнее правое ребро
"$UI" clk 190 430            # поле «Радиус» (сдвинулось вниз из-за «Очистить выбор»)
"$UI" key ctrl+a; "$UI" type 8; "$UI" key Return
"$UI" mousemove 1300 780
"$UI" shot fillet 2.5
