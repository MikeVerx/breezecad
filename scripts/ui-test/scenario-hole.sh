#!/bin/bash
# After scenario-box.sh: two counterbored holes in the top face, one of them
# removed again by clicking its centre, then a through hole on the front face.
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" key h                  # Отверстие
sleep 1.5
"$UI" clk 800 400            # верхняя грань: грань и первый центр
"$UI" clk 1000 470           # второй центр
"$UI" clk 195 498            # тип «Цековка» (ниже списка из двух центров)
"$UI" mousemove 1300 780
"$UI" shot hole-preview 2
"$UI" clk 1000 470           # щелчок по центру убирает его
"$UI" key Return             # Готово
sleep 2
"$UI" shot hole 1
