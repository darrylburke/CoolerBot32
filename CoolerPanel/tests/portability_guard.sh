#!/usr/bin/env bash
# Fail if firmware/shared/ references any platform-only symbol.
# lv_conf.h is excluded: it's LVGL's own build-time backend config (LV_USE_SDL,
# LV_SDL_INCLUDE_PATH, LV_USE_FREETYPE), not application logic reaching for a
# platform API directly -- it necessarily differs per target (desktop vs MCU)
# and is exactly the kind of platform selection this guard is meant to keep out
# of shared/*.cpp, not itself.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
hits=$(grep -rnE --exclude='lv_conf.h' 'SDL_|SDL\.h|mosquitto|freetype|ft2build|stb_image|MQTTClient|MQTTAsync' "$here/shared" || true)
if [ -n "$hits" ]; then
  echo "PORTABILITY VIOLATION: shared/ must call only LVGL"; echo "$hits"; exit 1
fi
echo "portability guard: clean"
