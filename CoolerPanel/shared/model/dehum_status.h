#pragma once
#include <stddef.h>
#include <stdint.h>
#include "platform.h"
struct DehumState;

// The Temp/Humidity page's words, the app's DehumStatusText and DehumHealth
// (CoolerApp/.../model/). Pure functions (no LVGL), unit-tested.

// "TIMER 1 h 23 m left", "CYCLE run 12 m left", "CYCLE rest 5 m left",
// "AUTO (rules)", "ON", "OFF"; "--" before any dehum/data or for an unknown mode.
const char* dehum_mode_text(const DehumState& s, char* out, size_t cap);

// AUTO only: "rules met: 1, 3" / "no rule met"; "" in any other mode.
const char* dehum_auto_text(const DehumState& s, char* out, size_t cap);

// "Relay 1 on • Relay 2 off"; "" before any dehum/data. (The app's middle dot
// is not in the panel's fonts; the bullet is, and the Trend page already uses it.)
const char* dehum_relays_text(const DehumState& s, char* out, size_t cap);

// The three above, the empty ones left out, joined by "  •  ".
const char* dehum_status_line(const DehumState& s, char* out, size_t cap);

// How the dehumidifier looks from here, judged apart from the Cooler.
enum class DehumView { BrokerDown, Offline, Waiting, Silent, Online };

// It publishes every 30 s: three missed messages is silence.
constexpr int64_t DEHUM_SILENT_AFTER_S = 90;

// In the app's order: the broker link first, then dehum/availability's
// "offline", then no dehum/data yet, then data gone quiet. (The app's "no
// access" case needs SUBACK codes the panel's MQTT clients do not expose.)
DehumView dehum_health(link_state_t link, const DehumState& s, int64_t now_epoch);

// "Broker unreachable", "Dehumidifier offline", "Waiting for dehumidifier",
// "Dehumidifier silent: no data for 90 s", "Online".
const char* dehum_view_text(DehumView v);
