#!/bin/bash
# Face selection probe at 1440x900 (temporary).
UI="$(dirname "$0")/ui.sh"
bash "$(dirname "$0")/scenario-box.sh"
# window frame (left to right) inside the viewport
"$UI" mousemove 480 210; sleep 0.3; "$UI" mousedown 1; "$UI" mousemove 700 400; sleep 0.2; "$UI" mousemove 1340 850; sleep 0.5
"$UI" shot 05-window-dragging
"$UI" mouseup 1; "$UI" mousemove 1420 860; "$UI" shot 06-window-done
"$UI" clk 1300 820
# Shift: top face, then the right side face
"$UI" clk 850 400; "$UI" keydown shift; "$UI" clk 1160 700; "$UI" keyup shift; "$UI" mousemove 1420 860; "$UI" shot 07-two-faces
"$UI" clk 1300 820
# right click on the top face, appearance panel
"$UI" clk 850 400 3; "$UI" shot 10-menu
"$UI" clk 910 448; sleep 1; "$UI" shot 11-panel
"$UI" mousemove 200 600; sleep 0.3
for _ in $(seq 25); do "$UI" click 5; sleep 0.05; done
sleep 0.5; "$UI" shot 12-scrolled
# the logo row: its swatch has the logo's blue on white
Y=$(python3 - "$SHOTS/12-scrolled.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
best = None
for y in range(200, 820):
    blue = sum(1 for x in range(55, 100) if (lambda p: p[2] > 180 and p[0] < 80 and p[1] < 120)(im.getpixel((x, y))))
    if blue >= 3: best = y if best is None else best
    if best is not None and y - best > 30: break
print(best + 6 if best is not None else 0)
PY
)
echo "logo row y=$Y"
if [ "$Y" -gt 0 ]; then "$UI" clk 160 "$Y"; fi
"$UI" mousemove 1000 300; sleep 1; "$UI" shot 13-applied
"$UI" clk 264 820; sleep 1; "$UI" mousemove 1420 860; "$UI" shot 14-done
"$UI" clk 1300 820; sleep 0.5; "$UI" shot 15-final
"$UI" stop
