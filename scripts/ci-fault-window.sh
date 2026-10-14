#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# CI: crashes (or freezes) the packaged program on purpose and checks that
# the crash (hang) window comes up with a report; the window goes into the
# annotations as a picture (grey, in 4000-character notices: the API keeps
# 4096 characters of each and 10 notices of a step).
#
# Usage: ci-fault-window.sh crash|hang <program> <display number>
# An Xvfb of its own: xvfb-run takes the display away when the crashed
# program ends, before its window (another process) has drawn.
set -u
kind="${1:?crash or hang}"
program="${2:?program}"
display=":${3:?display number}"
cfg="$PWD/cfg-$kind"
mkdir -p shots
Xvfb "$display" -screen 0 1600x1000x24 > /dev/null 2>&1 &
xvfb=$!
sleep 2
DISPLAY="$display" XDG_CONFIG_HOME="$cfg" BZ_NO_SPLASH=1 BZ_FAULT_TEST="$kind" BZ_HANG_SECONDS=3 \
  BZ_CRASH_WINDOW_SHOT="$PWD/shots/$kind.ppm:10" env -u LD_LIBRARY_PATH "$program" > "fault-$kind.log" 2>&1 &
app=$!
for _ in $(seq 60); do [ -f "shots/$kind.ppm" ] && break; sleep 1; done
sleep 1   # the picture is written; let the file close
kill "$app" 2> /dev/null
kill "$xvfb" 2> /dev/null
report=$(ls "$cfg"/breezecad/reports/"$kind"-*.txt 2> /dev/null | head -1)
if [ -f "shots/$kind.ppm" ] && [ -n "$report" ]; then
  echo "::notice title=$kind window::$(head -c 600 "$report" | tr '\n' '|')"
  python3 scripts/ppm2png.py "shots/$kind.ppm" "shots/$kind-window.png" --gray --chunk 4000 --annotate "$kind window" && rm "shots/$kind.ppm"
else
  echo "::warning title=$kind window::no window or no report: $(tail -5 "fault-$kind.log" | tr '\n' ' ' | cut -c1-400)"
  exit 1
fi
