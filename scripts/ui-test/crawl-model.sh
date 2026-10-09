#!/bin/bash
# Crash hunt: presses every ribbon button of this tab and clicks around (1440x900 window).
# Prints CRASH lines; silence means none. Model crawl needs кронштейн.bzc in $BZ_TEST_PARTS.
cd /home/claude/breezecad
U=scripts/ui-test/ui.sh
S=${BZ_TEST_PARTS:-$(cd "$(dirname "$0")/../.." && pwd)/tests/data}
fresh() { $U start "$S/кронштейн.bzc" >/dev/null 2>&1; sleep 1; }
tools="141,115 215,115 297,95 297,119 304,143 384,115 458,115 532,115 616,95 690,95 800,95 786,119 784,143 915,95 901,119 905,143 999,115 1183,95 1189,119 1267,95 1279,119"
fresh
for t in $tools; do
  x=${t%,*}; y=${t#*,}
  $U clk $x $y; sleep 0.5
  for p in "892 417" "760 619" "1100 520" "960 700" "1168 577"; do $U clk $p; done
  $U key Return; sleep 0.8; $U key Escape; $U key Escape; sleep 0.3
  if ! pgrep -x breezecad >/dev/null; then echo "CRASH after tool at $t"; tail -5 /tmp/breezecad-ui.log; fresh; fi
done
$U shot crawl-model 0.5; echo done; $U stop
