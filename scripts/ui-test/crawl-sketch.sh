#!/bin/bash
# Crash hunt: presses every ribbon button of this tab and clicks around (1440x900 window).
# Prints CRASH lines; silence means none. Model crawl needs кронштейн.bzc in $BZ_TEST_PARTS.
# Every sketch ribbon tool on a sketch with a rectangle: clicks on and off the
# curves, right click, Esc. Reports tools after which the app is gone.
cd /home/claude/breezecad
U=scripts/ui-test/ui.sh
export SHOTS=${SHOTS:-/tmp/shots}
fresh() {
  $U start >/dev/null 2>&1; $U clk 460 224; sleep 1; $U clk 52 115; $U clk 1060 507; sleep 1.5
  $U key r; $U clk 800 350; $U mousemove 900 450; sleep 0.2; $U clk 1100 650; $U key Escape
}
tools="51,115 125,115 199,115 273,115 357,95 357,119 436,115 557,95 557,119 540,143 666,95 666,119 752,95 746,119 840,95 845,119 936,95 941,119 1031,95 1031,119 1087,95 1113,95 1139,95 1165,95 1191,95 1087,119 1113,119 1139,119 1165,119 1191,119 1087,143 1113,143 1139,143 1258,115 1380,95 1358,119"
fresh
for t in $tools; do
  x=${t%,*}; y=${t#*,}
  $U clk $x $y
  for p in "1101 500" "800 500" "950 350" "1000 560" "1101 650" "700 300"; do $U clk $p; done
  $U clk 950 500 3; sleep 0.2; $U key Escape
  if ! pgrep -x breezecad >/dev/null; then echo "CRASH after tool at $t"; tail -5 /tmp/breezecad-ui.log; fresh; fi
done
$U shot crawl-end 0.5; echo done
$U stop
