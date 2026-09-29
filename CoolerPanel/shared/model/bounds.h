#pragma once
#include <stddef.h>

// How the settings screen renders this value.
enum class Widget { Stepper, Preset, Toggle };

// Which Settings tab the value lives on.
enum class Group { Box, Coil, Timing };

struct Bound {
    const char* key;     // MQTT command key
    const char* label;   // UI label
    const char* unit;    // display unit, "" for none
    int lo;
    int hi;
    int step;            // 0 for Preset/Toggle widgets
    Widget widget;
    Group group;
};

extern const Bound BOUNDS[];
extern const size_t BOUNDS_N;

// Returns nullptr if the key is not a settable value.
const Bound* bounds_find(const char* key);

// Clamp v into the legal range for key. cur_fin_cutoff supplies the dynamic
// floor for "fin_recover", which must stay at least 1 C above fin_cutoff.
// Unknown keys pass through unchanged.
int bounds_clamp(const char* key, int v, int cur_fin_cutoff);
