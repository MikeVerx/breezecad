#!/bin/bash
# After scenario-box.sh: measure the top face against the front face, then
# two edges of the top (120 and 80 mm, at 90°).
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" clk 1110 94            # Измерить
sleep 1
"$UI" clk 850 420            # верхняя грань
"$UI" clk 750 600            # передняя грань
"$UI" mousemove 1300 780
"$UI" shot measure-faces 1.5
"$UI" clk 750 527            # верхнее переднее ребро (новое измерение)
"$UI" clk 1105 568           # верхнее правое ребро
"$UI" mousemove 1300 780
"$UI" shot measure-edges 1.5
"$UI" key Escape
