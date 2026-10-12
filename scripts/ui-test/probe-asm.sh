#!/bin/bash
# Assembly appearance probe at 1440x900 (temporary).
UI="$(dirname "$0")/ui.sh"
D="$HOME/asm"; mkdir -p "$D"
cp tests/data/кронштейн.bzc tests/data/кронштейны.bza "$D/"
# The part gets the logo as its body's look (picture inside the file).
python3 - "$D/кронштейн.bzc" "$HOME/.config/breezecad/appearances/user.logo.png" <<'PY'
import sys, zipfile, json, shutil
path, png = sys.argv[1], sys.argv[2]
z = zipfile.ZipFile(path); files = {n: z.read(n) for n in z.namelist()}; z.close()
doc = json.loads(files['document.json'])
doc['look'] = {"appearances": [{"id": "user.logo", "name": "Лого BreezeCAD", "face": "#FFFFFF", "edges": "#3D4552",
               "finish": "matte", "picture": "user.logo.png", "pictureSize": 60}],
               "bodies": [{"body": 26, "appearance": "user.logo"}], "faces": []}
files['document.json'] = json.dumps(doc, ensure_ascii=False).encode()
files['pictures/user.logo.png'] = open(png, 'rb').read()
with zipfile.ZipFile(path, 'w') as out:
    for n, b in files.items(): out.writestr(n, b)
PY
"$UI" start "$D/кронштейны.bza"; sleep 2
export DISPLAY=:99
for _ in $(seq 120); do xdotool search --name BreezeCAD >/dev/null 2>&1 && break; sleep 0.5; done
sleep 3; W=$(xdotool search --name BreezeCAD | head -1); xdotool windowmove "$W" 0 0; sleep 2
"$UI" shot a1-open 2
# Кронштейн:2 chosen in the tree, the toolbar picker paints only it
"$UI" clk 150 316; sleep 0.5; "$UI" shot a2-chosen
"$UI" clk 500 20; sleep 1; "$UI" shot a3-picker
"$UI" clk 500 200; sleep 1; "$UI" mousemove 1420 860; "$UI" shot a4-painted 1
# "Вид" tab of the ribbon, then the tools there
"$UI" clk 100 58; sleep 1; "$UI" shot a5-view-tab
"$UI" clk 50 100; sleep 1; "$UI" mousemove 1420 860; "$UI" shot b1-panel
"$UI" clk 130 687; sleep 1; "$UI" mousemove 1420 860; "$UI" shot b2-blued-steel
"$UI" clk 260 286; sleep 0.5            # Поверхность
"$UI" clk 130 721; sleep 0.5            # Чугун
"$UI" clk 845 368; sleep 1; "$UI" mousemove 1420 860; "$UI" shot b3-face
"$UI" clk 260 394; sleep 0.5; "$UI" shot b4-to-part   # В детали
"$UI" clk 265 821; sleep 4; "$UI" mousemove 1420 860; "$UI" shot b5-written 2
"$UI" stop
