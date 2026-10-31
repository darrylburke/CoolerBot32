#pragma once
#include <stddef.h>

// The dehumidifier's integer settings (RULES-dehum.md, spec
// docs/superpowers/specs/2026-10-31-dehum-rules-and-temp-humidity-tab-design.md §1).
// Mirrors the firmware's clamp_settings() and the app's DehumBounds.kt.
//
// Every key the firmware accepts on dehum/cmd and echoes in dehum/data has a
// fixed index: the four plain settings first, then rN_<field> for rules 1..4.
// Settings are stored and looked up by that index, so nothing allocates.

constexpr int DEHUM_RULES = 4;
constexpr int DEHUM_RULE_FIELDS = 6;
constexpr int DEHUM_PLAIN_N = 4;
constexpr int DEHUM_NKEYS = DEHUM_PLAIN_N + DEHUM_RULES * DEHUM_RULE_FIELDS;   // 28

// Plain settings, in screen order.
enum { DK_TIMER_MIN = 0, DK_CYCLE_ON = 1, DK_CYCLE_OFF = 2, DK_MIN_RUN = 3 };

// A rule's fields, in the order the rule keys are laid out.
enum DehumRuleField { RF_EN = 0, RF_SENSOR, RF_CMP, RF_VALUE, RF_RELAY, RF_ACTION };

// rN_sensor: 0 temperature, 1 humidity. rN_cmp: 0 below, 1 above.
// rN_relay: 1 dehumidifier, 2 spare. rN_action: 1 turn on, 0 turn off.
constexpr int DEHUM_SENSOR_TEMP = 0;
constexpr int DEHUM_SENSOR_HUM = 1;
constexpr int DEHUM_TEMP_LO = -20, DEHUM_TEMP_HI = 50;   // whole C
constexpr int DEHUM_HUM_LO = 0, DEHUM_HUM_HI = 100;      // whole %RH

struct DehumBound {
    const char* key;
    const char* label;
    const char* unit;
    int lo, hi, step;
};
// The four plain settings, indexed by DK_*.
extern const DehumBound DEHUM_PLAIN[DEHUM_PLAIN_N];

// A full set of setting values; known[i] is false until dehum/data carried
// key i (the app shows such a value as "--").
struct DehumValues {
    int  v[DEHUM_NKEYS] = {};
    bool known[DEHUM_NKEYS] = {};
};

const char* dehum_key(int idx);              // "timer_min" ... "r4_action"; nullptr if out of range
int  dehum_key_index(const char* key);       // -1 for anything the firmware does not accept
int  dehum_rule_key(int n, int field);       // index of rN_<field>, n = 1..4
// The rule number (1..4) and field of a rule key; false for a plain setting.
bool dehum_rule_of(int idx, int& n, int& field);

// The legal values now, given the other current values: a cycle's run cannot
// be shorter than the minimum run, and a rule's threshold range follows its
// sensor (temperature only when the sensor is known to be 0, as the app
// decides it; otherwise humidity). False for a bad index.
bool dehum_range(int idx, const DehumValues& cur, int& lo, int& hi);
int  dehum_step(int idx);                    // the - / + step
// "min", "°C", "%RH" or "" (rule choices have no unit).
const char* dehum_unit(int idx, const DehumValues& cur);
// "120 min", "65 %RH", "-5 °C", "--" when unknown.
void dehum_format(int idx, int v, bool known, const DehumValues& cur, char* out, size_t n);
// A name for messages: "Timer", "Rule 2 threshold".
void dehum_label(int idx, char* out, size_t n);
