#!/bin/bash
# Face selection probe at 1440x900 (temporary).
UI="$(dirname "$0")/ui.sh"
"$UI" start; sleep 4
# "Новая деталь" until a part is open (the home page has no ribbon tabs)
for try in 1 2 3 4 5; do
  "$UI" clk 460 224; sleep 2; "$UI" shot start-$try 0.3
  python3 - "$SHOTS/start-$try.png" <<'PY' && break
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
# a part shows the ribbon: the "3D-модель" tab underline (accent blue) near the top left
sys.exit(0 if any(im.getpixel((x, 60))[2] > 180 and im.getpixel((x, 60))[0] < 90 for x in range(10, 120)) else 1)
PY
done
sed -n '/Начать эскиз/,$p' "$(dirname "$0")/scenario-box.sh" | sed '/shot box/d' > /tmp/box-rest.sh
UI="$UI" bash /tmp/box-rest.sh
"$UI" shot box 1
# Shift: top face, then the right side face; right click keeps both
"$UI" clk 850 400; "$UI" keydown shift; "$UI" clk 1080 645; "$UI" keyup shift; "$UI" mousemove 1420 860; "$UI" shot 07-two-faces
"$UI" clk 850 400 3; "$UI" shot 10-menu
"$UI" clk 910 448; sleep 1; "$UI" shot 11-panel
"$UI" mousemove 200 600; sleep 0.3
for _ in $(seq 25); do "$UI" click 5; sleep 0.05; done
sleep 0.5; "$UI" shot 12-scrolled
"$UI" clk 150 720
"$UI" mousemove 1000 300; sleep 1; "$UI" shot 13-applied
"$UI" clk 264 820; sleep 1; "$UI" mousemove 1420 860; "$UI" shot 14-done
"$UI" clk 1300 820; sleep 0.5; "$UI" shot 15-final
"$UI" stop
