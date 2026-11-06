#!/bin/bash
# Окно «Обнаружено изменение, затрагивающее другие детали» (ADR-0009).
# Сборка «Узел»: «Короб» построен вокруг «Основания» проекциями его эскиза.
# Пока сборка открыта, файл основания заменяется версией с выступом (как
# сохранение детали) — открывается окно; снимки каждого варианта, затем
# «Повторить контур»: короб обходит выступ.
#   bash scripts/ui-test/scenario-change-dialog.sh      (снимки в $SHOTS)
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
UI="$ROOT/scripts/ui-test/ui.sh"
DIR=${CD_DIR:-/tmp/bz-change-dialog}
rm -rf "$DIR"
BZ_UI_FIXTURE_DIR="$DIR" "$ROOT/build/linux-debug/bin/bz_tests" -tc="*фикстура UI-сценария*" >/dev/null
"$UI" start "$DIR/Узел.bza"
sleep 3
"$UI" shot change-0-open 1
cp "$DIR/Основание-выступ.bzc" "$DIR/Основание.bzc"   # «сохранили» основание с выступом
sleep 6
"$UI" mousemove 1000 820
"$UI" shot change-1-auto 2          # по умолчанию — автоадаптация
"$UI" clk 330 270; sleep 2; "$UI" mousemove 1000 820
"$UI" shot change-2-ignore 2
"$UI" clk 330 428; sleep 2; "$UI" mousemove 1000 820
"$UI" shot change-3-manual 2
"$UI" clk 330 380; sleep 2; "$UI" mousemove 1000 820
"$UI" shot change-4-mimic 2
"$UI" clk 1125 747; sleep 4          # «Повторить контур»
"$UI" mousemove 900 600
"$UI" shot change-5-applied 2
grep -q "Короб: Мимикрирование с зазором" /tmp/breezecad-ui.log && echo "OK: мимикрирование применено" || { echo "FAIL"; exit 1; }
"$UI" stop
