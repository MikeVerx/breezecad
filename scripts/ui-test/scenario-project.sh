#!/bin/bash
# After scenario-box.sh: a sketch on the top face, two top edges projected
# into it (amber, fixed, they follow the model).
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" key s                  # Начать эскиз
sleep 1
"$UI" clk 850 420            # верхняя грань
sleep 2.5
"$UI" clk 427 100            # Проецировать
"$UI" clk 700 219            # верхнее ребро
"$UI" clk 459 600            # левое ребро
"$UI" mousemove 900 600
"$UI" shot project 1
