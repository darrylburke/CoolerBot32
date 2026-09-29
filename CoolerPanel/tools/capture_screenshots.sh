#!/usr/bin/env bash
# Regenerates the panel screenshots in ../docs/screenshots/ from the desktop
# simulator, using the recorded /data fixtures (no broker connection).
# Build the simulator first (see README.md); run from anywhere.
set -euo pipefail
PANEL="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$PANEL/../docs/screenshots"
SIM="${SIM:-$PANEL/build/v4/cooler_sim}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"
cd "$PANEL"   # the simulator loads assets/ relative to here

# The fixtures predate the 1 °C ice-cutoff default; show the current one.
for f in normal override defrost noresponse finproxy; do
    sed 's/"fin_cutoff":0/"fin_cutoff":1/' "tests/fixtures/data_$f.json" > "$TMP/$f.json"
done

shot() { SDL_VIDEODRIVER=dummy "$SIM" --frames 200 "$@"; }
shot --fixture "$TMP/normal.json"     --seed-history --page 0 --screenshot "$OUT/panel-trend.png"
shot --fixture "$TMP/override.json"   --seed-history --page 0 --screenshot "$OUT/panel-trend-override.png"
shot --fixture "$TMP/defrost.json"    --seed-history --page 0 --screenshot "$OUT/panel-trend-defrost.png"
shot --fixture "$TMP/noresponse.json" --seed-history --page 0 --screenshot "$OUT/panel-alarm-ac.png"
shot --fixture "$TMP/finproxy.json"   --seed-history --page 0 --screenshot "$OUT/panel-alarm-sensor.png"
shot --fixture "$TMP/normal.json" --page 1 --tab 0 --screenshot "$OUT/panel-settings-box.png"
shot --fixture "$TMP/normal.json" --page 1 --tab 1 --screenshot "$OUT/panel-settings-coil.png"
shot --fixture "$TMP/normal.json" --page 1 --tab 2 --screenshot "$OUT/panel-settings-timing.png"
shot --fixture "$TMP/normal.json" --page 2 --screenshot "$OUT/panel-detail.png"
shot --screen boot --screenshot "$OUT/panel-boot.png"
