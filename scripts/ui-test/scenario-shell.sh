#!/bin/bash
# After scenario-box.sh: a 2 mm shell open at the top and the front, then the
# front closed again by a second click; Enter finishes.
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" clk 610 118            # Оболочка
sleep 1.5
"$UI" clk 850 420            # верхняя грань — открыть
"$UI" clk 750 600            # передняя грань — открыть
"$UI" mousemove 1300 780
"$UI" shot shell-preview 2
"$UI" clk 750 600            # передняя грань — снова закрыть
"$UI" key Return             # Готово
sleep 2
"$UI" mousemove 1300 780
"$UI" shot shell 1
