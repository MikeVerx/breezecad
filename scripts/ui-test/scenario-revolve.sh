#!/bin/bash
# Smoke scenario at 1440x900: a rectangle beside a line along the sketch Y
# axis, turned into a tube around that line with "Вращение".
set -e
UI="$(dirname "$0")/ui.sh"
"$UI" start
"$UI" clk 599 224            # Новая деталь
sleep 1
"$UI" clk 52 115             # Начать эскиз
"$UI" clk 1060 507           # плоскость XY
sleep 1.5
"$UI" key r                    # Прямоугольник справа от оси Y
"$UI" clk 950 420
"$UI" mousemove 1000 500; sleep 0.3
"$UI" clk 1080 600
"$UI" key Escape
"$UI" key l                    # Отрезок вдоль оси Y — ось вращения
"$UI" clk 880 300
"$UI" clk 880 700
"$UI" key Escape Escape Escape Escape   # Принять эскиз: Esc обрывает цепочку, снимает инструмент, выбор, затем выходит
sleep 1.5
"$UI" clk 215 115            # Вращение
sleep 1.5
"$UI" clk 730 413            # щелчок по отрезку: это ось (полный оборот)
sleep 1
"$UI" clk 288 821            # Готово
"$UI" mousemove 900 820
"$UI" shot tube 2
