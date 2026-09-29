#!/usr/bin/env bash
# Capture live /data payloads from the cooler into tests/fixtures/.
set -euo pipefail
# Credentials come from the environment so this script carries no secret.
#   export COOLER_MQTT_PASS=...   (or source it from your shell profile)
H="${COOLER_MQTT_HOST:-mqtt.example.com}"
U="${COOLER_MQTT_USER:-}"
P="${COOLER_MQTT_PASS:?set COOLER_MQTT_PASS before running}"
T=ha/esp32-cooler/data
D="$(cd "$(dirname "$0")" && pwd)/fixtures"
mkdir -p "$D"
name="${1:?usage: capture_fixtures.sh <name>}"
mosquitto_sub -h "$H" -p 1883 -u "$U" -P "$P" -t "$T" -C 1 > "$D/data_$name.json"
echo "wrote $D/data_$name.json ($(wc -c < "$D/data_$name.json") bytes)"
