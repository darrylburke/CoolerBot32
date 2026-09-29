#include "bounds.h"
#include <cstring>

// Mirrors the v4 controller's settings table (spec §2.11). Two deliberate
// divergences, both panel-side only:
//   - dutypercent floors at 5 not 1 (a step-5 stepper from 1 is nonsense, and
//     sub-5 duty is moot once minruntime forces a floor on the actual run)
//   - sampleinterval uses preset chips, not a stepper
const Bound BOUNDS[] = {
    {"coolerset",      "Set point",    "C",    2,   40,  1, Widget::Stepper, Group::Box},
    {"range",          "Range +/-",    "C",    0,    5,  1, Widget::Stepper, Group::Box},
    {"sampleinterval", "Sample every", "s",   10, 3600,  0, Widget::Preset,  Group::Box},
    {"fin_cutoff",     "Ice cutoff",   "C",   -5,    5,  1, Widget::Stepper, Group::Coil},
    {"fin_recover",    "Ice clear",    "C",   -4,   10,  1, Widget::Stepper, Group::Coil},
    {"settle",         "Settle",       "min",  2,   30,  1, Widget::Stepper, Group::Coil},
    {"minofftime",     "Min off",      "min",  0,   30,  1, Widget::Stepper, Group::Timing},
    {"minruntime",     "Min run",      "s",    0,  600, 30, Widget::Stepper, Group::Timing},
    {"maxrun",         "Max run",      "min",  1,   60,  1, Widget::Stepper, Group::Timing},
    {"dutypercent",    "Backup duty",  "%",    5,  100,  5, Widget::Stepper, Group::Timing},
};
const size_t BOUNDS_N = sizeof(BOUNDS) / sizeof(BOUNDS[0]);

const Bound* bounds_find(const char* key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < BOUNDS_N; i++)
        if (std::strcmp(BOUNDS[i].key, key) == 0) return &BOUNDS[i];
    return nullptr;
}

int bounds_clamp(const char* key, int v, int cur_fin_cutoff) {
    const Bound* b = bounds_find(key);
    if (!b) return v;
    int lo = b->lo;
    if (std::strcmp(key, "fin_recover") == 0) {
        int dynamic_floor = cur_fin_cutoff + 1;
        if (dynamic_floor > lo) lo = dynamic_floor;
    }
    if (v < lo) return lo;
    if (v > b->hi) return b->hi;
    return v;
}
