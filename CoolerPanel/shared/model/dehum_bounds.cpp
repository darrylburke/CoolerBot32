#include "dehum_bounds.h"
#include <cstdio>
#include <cstring>

const DehumBound DEHUM_PLAIN[DEHUM_PLAIN_N] = {
    {"timer_min", "Timer",      "min", 1, 1440, 15},
    {"cycle_on",  "Cycle run",  "min", 1,  240,  5},
    {"cycle_off", "Cycle rest", "min", 1,  240,  5},
    {"min_run",   "Min run",    "min", 0,   60,  1},
};

static const char* const kKeys[DEHUM_NKEYS] = {
    "timer_min", "cycle_on", "cycle_off", "min_run",
    "r1_en", "r1_sensor", "r1_cmp", "r1_value", "r1_relay", "r1_action",
    "r2_en", "r2_sensor", "r2_cmp", "r2_value", "r2_relay", "r2_action",
    "r3_en", "r3_sensor", "r3_cmp", "r3_value", "r3_relay", "r3_action",
    "r4_en", "r4_sensor", "r4_cmp", "r4_value", "r4_relay", "r4_action",
};

static const char* const kFieldWords[DEHUM_RULE_FIELDS] = {
    "enabled", "sensor", "above/below", "threshold", "relay", "on/off",
};

static const char* const kDegC = "°C";

const char* dehum_key(int idx) {
    return (idx >= 0 && idx < DEHUM_NKEYS) ? kKeys[idx] : nullptr;
}

int dehum_key_index(const char* key) {
    if (!key) return -1;
    for (int i = 0; i < DEHUM_NKEYS; i++)
        if (!std::strcmp(kKeys[i], key)) return i;
    return -1;
}

int dehum_rule_key(int n, int field) {
    if (n < 1 || n > DEHUM_RULES || field < 0 || field >= DEHUM_RULE_FIELDS) return -1;
    return DEHUM_PLAIN_N + (n - 1) * DEHUM_RULE_FIELDS + field;
}

bool dehum_rule_of(int idx, int& n, int& field) {
    if (idx < DEHUM_PLAIN_N || idx >= DEHUM_NKEYS) return false;
    n = (idx - DEHUM_PLAIN_N) / DEHUM_RULE_FIELDS + 1;
    field = (idx - DEHUM_PLAIN_N) % DEHUM_RULE_FIELDS;
    return true;
}

// A rule's threshold is in degrees only when its sensor is known to be the
// temperature one -- the app's choice, so the two never disagree on a range.
static bool rule_is_temp(int n, const DehumValues& cur) {
    const int s = dehum_rule_key(n, RF_SENSOR);
    return cur.known[s] && cur.v[s] == DEHUM_SENSOR_TEMP;
}

bool dehum_range(int idx, const DehumValues& cur, int& lo, int& hi) {
    if (idx >= 0 && idx < DEHUM_PLAIN_N) {
        lo = DEHUM_PLAIN[idx].lo;
        hi = DEHUM_PLAIN[idx].hi;
        // The firmware raises cycle_on with min_run, so a run below it would
        // only be clamped back up.
        if (idx == DK_CYCLE_ON && cur.known[DK_MIN_RUN] && cur.v[DK_MIN_RUN] > lo)
            lo = cur.v[DK_MIN_RUN];
        return true;
    }
    int n, field;
    if (!dehum_rule_of(idx, n, field)) return false;
    switch (field) {
    case RF_RELAY: lo = 1; hi = 2; break;
    case RF_VALUE:
        if (rule_is_temp(n, cur)) { lo = DEHUM_TEMP_LO; hi = DEHUM_TEMP_HI; }
        else                      { lo = DEHUM_HUM_LO;  hi = DEHUM_HUM_HI; }
        break;
    default: lo = 0; hi = 1; break;
    }
    return true;
}

int dehum_step(int idx) {
    return (idx >= 0 && idx < DEHUM_PLAIN_N) ? DEHUM_PLAIN[idx].step : 1;
}

const char* dehum_unit(int idx, const DehumValues& cur) {
    if (idx >= 0 && idx < DEHUM_PLAIN_N) return DEHUM_PLAIN[idx].unit;
    int n, field;
    if (!dehum_rule_of(idx, n, field) || field != RF_VALUE) return "";
    return rule_is_temp(n, cur) ? kDegC : "%RH";
}

void dehum_format(int idx, int v, bool known, const DehumValues& cur, char* out, size_t n) {
    if (!known) { std::snprintf(out, n, "--"); return; }
    const char* u = dehum_unit(idx, cur);
    if (*u) std::snprintf(out, n, "%d %s", v, u);
    else    std::snprintf(out, n, "%d", v);
}

void dehum_label(int idx, char* out, size_t n) {
    if (idx >= 0 && idx < DEHUM_PLAIN_N) { std::snprintf(out, n, "%s", DEHUM_PLAIN[idx].label); return; }
    int r, field;
    if (dehum_rule_of(idx, r, field)) std::snprintf(out, n, "Rule %d %s", r, kFieldWords[field]);
    else                              std::snprintf(out, n, "?");
}
