#!/bin/bash
# Drives a running BreezeCAD on a virtual X display (Linux, for testing).
#
#   ui.sh start [file.bzc]   start Xvfb :99 (if needed) and the app
#   ui.sh stop
#   ui.sh shot NAME [delay]  save the app's own frame to $SHOTS/NAME.png
#   ui.sh clk X Y [button]   move, pause like a person, click
#   ui.sh key CHORD...       e.g. ctrl+s, Return, Escape
#   ui.sh type TEXT
#   ui.sh <xdotool args>     anything else goes to xdotool
#
# Screenshots come from the app (BZ_SHOT_DIR hook): an X grab of a Mesa
# GL window on Xvfb is black. Needs: xvfb xdotool python3-pil.
set -e
# UI_DISPLAY / UI_SCREEN / UI_SIZE: another display, e.g. a 2x one for
# sharp screenshots (UI_SCREEN=2880x1800 UI_SIZE="2880 1800", uiScale 2).
export DISPLAY="${UI_DISPLAY:-:99}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BZ_BIN:-$ROOT/build/linux-debug/bin}"
SHOTS="${SHOTS:-/tmp/shots}"
case "$1" in
  start)
    pgrep -f "Xvfb $DISPLAY " >/dev/null || (Xvfb "$DISPLAY" -screen 0 "${UI_SCREEN:-1600x1000}x24" >/dev/null 2>&1 &)
    sleep 0.5
    pkill -x breezecad || true
    sleep 0.3
    mkdir -p "$SHOTS"; rm -f "$SHOTS/request"
    # A UTF-8 locale: zenity (file dialogs) rejects Cyrillic arguments otherwise.
    (cd "$BIN" && LIBGL_ALWAYS_SOFTWARE=1 LANG=C.UTF-8 BZ_SHOT_DIR="$SHOTS" ./breezecad $2 >/tmp/breezecad-ui.log 2>&1 &)
    sleep 4
    W=$(xdotool search --name BreezeCAD | head -1)
    xdotool windowmove "$W" 0 0
    if [ -n "$UI_SIZE" ]; then xdotool windowsize "$W" $UI_SIZE; sleep 1; fi
    ;;
  stop) pkill -x breezecad || true ;;
  shot)
    sleep "${3:-0.6}"
    rm -f "$SHOTS/$2.ppm"; echo "$2" > "$SHOTS/request"
    for _ in $(seq 40); do [ -f "$SHOTS/$2.ppm" ] && break; xdotool mousemove_relative 0 0; sleep 0.25; done
    sleep 0.2
    python3 -c "from PIL import Image; Image.open('$SHOTS/$2.ppm').save('$SHOTS/$2.png')"
    ;;
  clk)
    xdotool mousemove "$2" "$3"; sleep 0.3
    xdotool mousemove $(( $2 + 1 )) "$3"; sleep 0.2
    xdotool click "${4:-1}"; sleep 0.4
    ;;
  key) shift; xdotool key "$@"; sleep 0.4 ;;
  type) xdotool type --delay 60 "$2"; sleep 0.3 ;;
  *) xdotool "$@" ;;
esac
