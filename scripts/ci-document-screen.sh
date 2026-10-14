#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# CI: does the window still update once a document is open? Pictures of the
# real X screen (not of the program's OpenGL buffer, which looked right even
# while the screen was frozen): one on the home page, one after a new part
# opened (BZ_TEST_NEW_PART). They must differ. Before the fix of 2026-10 the
# second showed the home page again: on Linux the window froze on the last
# picture before the first 3D render.
#
# Usage: ci-document-screen.sh <program> <display number>
set -u
program="${1:?program}"
display=":${2:?display number}"
mkdir -p shots
Xvfb "$display" -screen 0 1600x1000x24 > /dev/null 2>&1 &
xvfb=$!
sleep 2
DISPLAY="$display" XDG_CONFIG_HOME="$PWD/cfg-document" BZ_NO_SPLASH=1 BZ_TEST_NEW_PART=6 \
  env -u LD_LIBRARY_PATH "$program" > document.log 2>&1 &
app=$!
sleep 4
DISPLAY="$display" import -window root -depth 8 shots/home-screen.ppm
sleep 8
DISPLAY="$display" import -window root -depth 8 shots/document-screen.ppm
kill "$app" 2> /dev/null
kill "$xvfb" 2> /dev/null
changed=$(python3 - shots/home-screen.ppm shots/document-screen.ppm <<'PY'
import sys
def read(path):
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace(): pos += 1
        if data[pos:pos + 1] == b"#": pos = data.index(b"\n", pos) + 1; continue
        end = pos
        while not data[end:end + 1].isspace(): end += 1
        fields.append(data[pos:end]); pos = end
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]
w, h, a = read(sys.argv[1]); _, _, b = read(sys.argv[2])
changed = sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3]) / (w * h)
print(f"{changed * 100:.1f}")
PY
)
opened=$(grep -c "OpenGL after the 3D view" document.log)
echo "screen changed by ${changed:-?}% after the part opened (3D renders logged: $opened)"
python3 scripts/ppm2png.py shots/document-screen.ppm shots/document-screen.png --crop 400,250,800,500 --half --gray \
  --chunk 4000 --annotate "document screen"
if [ "$opened" -gt 0 ] && python3 -c "import sys; sys.exit(0 if float('${changed:-0}') >= 5 else 1)"; then
  echo "::notice title=Document screen::the window shows the open part (${changed}% of the screen changed)"
else
  echo "::warning title=Document screen::the screen did not change after a part opened (${changed:-?}%, 3D renders: $opened): frozen window? $(tail -3 document.log | tr '\n' ' ' | cut -c1-300)"
  exit 1
fi
