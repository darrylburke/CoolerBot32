#pragma once
#include <stdint.h>
#include <stddef.h>
#include "dehum_bounds.h"

// The dehumidifier's fixed topics: set by its firmware and the Node-RED flow,
// not by the panel's Cooler topic base.
#define DEHUM_TOPIC_DATA          "dehum/data"
#define DEHUM_TOPIC_AVAILABILITY  "dehum/availability"
#define DEHUM_TOPIC_HISTORY       "dehum/history"
#define DEHUM_TOPIC_CMD           "dehum/cmd"

enum class DehumMode : uint8_t { Unknown, Auto, On, Off, Timer, Cycle };

// The mode buttons, in screen order (the app's DehumMode.BUTTONS).
constexpr int DEHUM_MODE_BUTTONS_N = 5;
extern const DehumMode DEHUM_MODE_BUTTONS[DEHUM_MODE_BUTTONS_N];

const char* dehum_mode_wire(DehumMode m);    // "auto", ... ; "" for Unknown
const char* dehum_mode_label(DehumMode m);   // "AUTO (rules)", "ON", ... ; "--" for Unknown
DehumMode   dehum_mode_of(const char* wire); // Unknown for anything else

// One dehum/data snapshot (RULES-dehum.md, schema "v": 1). Each message is
// complete, so a parse replaces every field: a null or absent reading is no
// reading, an absent setting is unknown (as the app's DehumStateParser does).
struct DehumState {
    DehumMode mode = DehumMode::Unknown;
    bool relay = false;          // relay 1, the dehumidifier
    bool relay2 = false;         // the spare; only AUTO's rules drive it
    bool link_lost = false;      // the dehumidifier has lost the broker: both relays off
    uint32_t hold_s = 0, hold2_s = 0;   // min run left, relay 1 / 2
    uint32_t remain_s = 0;       // TIMER: time left; CYCLE: time left in this phase
    int phase = -1;              // CYCLE: 1 run, 0 rest; -1 otherwise (null)
    float temp = 0.0f;     bool temp_valid = false;
    float humidity = 0.0f; bool humidity_valid = false;
    bool rule_match[DEHUM_RULES] = {};   // rN_match: rule N's condition holds now
    DehumValues settings;
    uint32_t run_s = 0;
    uint32_t uptime_s = 0;

    // ---- panel-side meta ----
    int64_t last_rx_epoch = 0;
    bool valid = false;              // a dehum/data has parsed
    bool online = false;             // dehum/availability
    bool availability_seen = false;
};

// Parse a dehum/data payload into s. Rejects (false, s untouched) anything that
// is not a JSON object with "v": 1. Keeps s's availability fields.
bool dehum_parse_data(DehumState& s, const char* json, size_t len, int64_t now_epoch);

// dehum/availability: "online" or "offline" (LWT). False for anything else.
bool dehum_parse_availability(DehumState& s, const char* payload, size_t len);
