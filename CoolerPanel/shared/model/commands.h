#pragma once
#include <stdint.h>
#include <stddef.h>
#include "cooler_state.h"

static const uint32_t CMD_DEBOUNCE_MS = 400;
static const uint32_t CMD_DEADLINE_MS = 3000;
static const size_t   CMD_MAX_PENDING = 12;  // headroom over the 10 settable keys

class Commands {
public:
    // Queue a change. Clamped against the bounds table using the last known
    // fin_cutoff. Repeated calls for the same key inside the debounce window
    // coalesce into one publish of the final value.
    void request(const char* key, int value, uint32_t now_ms);

    // Drive debounce flushes and deadline expiry. Call every UI tick.
    void tick(uint32_t now_ms);

    // Feed every fresh /data. Clears pending entries the cooler has answered.
    void on_state(const CoolerState& s);

    bool is_pending(const char* key) const;

    // What the UI should show: the optimistic requested value while pending,
    // otherwise the authoritative value from state.
    int display_value(const char* key, const CoolerState& s) const;

    // Fire-and-forget action ({"calibrate":1}, {"fincal_reset":1}): published
    // at once, no debounce and no pending/reconcile -- its effect shows up in
    // later /data fields (cal_active, fin_cal) rather than as an echoed value.
    // Returns false and raises a toast if the publish fails.
    bool action(const char* key, int value);

    // One-shot failure notice. Returns false when there is nothing to show.
    bool take_toast(char* out, size_t n);

private:
    struct Entry {
        char key[20] = {0};
        int  want = 0;
        int  pre = 0;          // value at publish time, for the clear rule
        uint32_t queued_ms = 0;
        uint32_t sent_ms = 0;
        bool active = false;
        bool sent = false;
    };
    Entry entries_[CMD_MAX_PENDING];
    CoolerState last_;         // most recent /data, for pre-values
    int last_fin_cutoff_ = 0;
    char toast_[64] = {0};
    bool has_toast_ = false;

    Entry* find(const char* key);
    const Entry* find(const char* key) const;
    Entry* alloc(const char* key);
    void   set_toast(const char* key);
};
