#!/bin/bash
# Face selection probe at 1440x900 (temporary).
UI="$(dirname "$0")/ui.sh"
bash "$(dirname "$0")/scenario-box.sh"
"$UI" shot 00-box 1
"$UI" mousemove 850 450; sleep 0.5; "$UI" shot 01-hover
"$UI" clk 850 450;  "$UI" mousemove 1300 820; "$UI" shot 02-clicked
"$UI" keydown shift; "$UI" clk 850 560; "$UI" keyup shift; "$UI" mousemove 1300 820; "$UI" shot 03-shift-click
"$UI" clk 1300 820; "$UI" shot 04-empty-click
# window frame, then crossing frame
"$UI" mousemove 450 150; sleep 0.3; "$UI" mousedown 1; "$UI" mousemove 700 300; sleep 0.2; "$UI" mousemove 1350 830; sleep 0.5
"$UI" shot 05-window-dragging
"$UI" mouseup 1; "$UI" mousemove 1400 860; "$UI" shot 06-window-done
"$UI" clk 1300 820
"$UI" mousemove 1350 830; sleep 0.3; "$UI" mousedown 1; "$UI" mousemove 1100 600; sleep 0.2; "$UI" mousemove 850 450; sleep 0.5
"$UI" shot 07-crossing-dragging
"$UI" mouseup 1; "$UI" mousemove 1400 860; "$UI" shot 08-crossing-done
"$UI" key Escape; "$UI" shot 09-esc
# right click on a face: the menu
"$UI" clk 850 450 3; "$UI" shot 10-menu
"$UI" clk 910 498; sleep 1; "$UI" shot 11-panel
"$UI" stop
