#!/bin/bash
# Smoke scenario at 1440x900: new part, sketch on XY, 120x80 rectangle,
# extrude 30 mm. Coordinates match the default layout at 100 % scale.
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" start
"$UI" clk 460 224            # Новая деталь
sleep 1
"$UI" clk 52 115             # Начать эскиз
"$UI" clk 1060 507           # плоскость XY (правый угол ромба)
sleep 1.5
"$UI" clk 125 115            # Прямоугольник
"$UI" clk 700 380
"$UI" mousemove 900 500; sleep 0.3
"$UI" clk 1080 640
"$UI" clk 957 115            # Размер
"$UI" clk 800 380            # верхний отрезок
"$UI" mousemove 800 340; sleep 0.3
"$UI" clk 800 340
"$UI" key ctrl+a; "$UI" type 120; "$UI" key Return
sleep 1
"$UI" clk 557 600            # левый отрезок
"$UI" mousemove 510 600; sleep 0.3
"$UI" clk 510 600
"$UI" key ctrl+a; "$UI" type 80; "$UI" key Return
sleep 1
"$UI" clk 1135 115           # Принять эскиз
sleep 1.5
"$UI" clk 140 115            # Выдавливание
sleep 1.5
"$UI" key ctrl+a; "$UI" type 30; "$UI" key Return   # Enter = Готово
sleep 2
"$UI" shot box 1
