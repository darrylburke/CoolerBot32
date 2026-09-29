#!/usr/bin/env bash
# Copies the broker's private CA (LLMMon Private CA) out of the controller's
# ESPHome config (mqtt: certificate_authority) into the app's assets, where
# MqttRepository makes it the only trusted CA. The output is git-ignored.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
YAML="${1:-$HERE/../controller/cooler-v4.yaml}"
OUT="$HERE/app/src/main/assets/broker_ca.pem"
mkdir -p "$(dirname "$OUT")"
awk '/certificate_authority: \|-?$/ { grab = 1; next }
     grab && /^    / { sub(/^    /, ""); print; next }
     grab { exit }' "$YAML" > "$OUT.tmp"
if ! grep -q -- '-----BEGIN CERTIFICATE-----' "$OUT.tmp" || ! grep -q -- '-----END CERTIFICATE-----' "$OUT.tmp"; then
    rm -f "$OUT.tmp"
    echo "no certificate_authority block found in $YAML" >&2
    exit 1
fi
mv "$OUT.tmp" "$OUT"
openssl x509 -in "$OUT" -noout -subject
