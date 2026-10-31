#pragma once
#include <stdint.h>
#include <stddef.h>
#include "dehum_state.h"

// dehum/cmd from the panel: the Cooler settings' pattern (commands.h) for the
// dehumidifier's keys. A change is held for DEHUM_DEBOUNCE_MS so a run of taps
// sends one value, published as {"<key>":<int>} (never retained), then shown
// pending until a dehum/data reflects it; after DEHUM_DEADLINE_MS without that
// the edit reverts and a "Not applied: ..." notice is raised. Mode buttons
// are sent at once and are not pending (the highlight follows dehum/data).
// Wording follows the app's DehumController.
//
// `enabled` is the page's "controls work only while the dehumidifier is
// online": every entry point refuses (and a queued edit is dropped with
// "Couldn't send ...") while it is false.
static const uint32_t DEHUM_DEBOUNCE_MS = 400;
static const uint32_t DEHUM_DEADLINE_MS = 3000;

class DehumCommands {
public:
    // Publish {"mode":"<wire>"} now. False (with a notice) when not sent.
    bool set_mode(DehumMode m, bool enabled);

    // - / +: one step from the shown value, kept inside the live range.
    // False when refused or when the value would not change.
    bool step(int idx, int dir, const DehumState& s, bool enabled, uint32_t now_ms);

    // An exact value (a rule choice): refused (false) outside the live range.
    bool enter(int idx, int value, const DehumState& s, bool enabled, uint32_t now_ms);

    // Flush debounced edits and expire unanswered ones. Call every UI tick.
    void tick(uint32_t now_ms, const DehumState& s, bool enabled);

    // Feed every dehum/data parse. Clears the edits it answers.
    void on_state(const DehumState& s);

    bool is_pending(int idx) const;   // queued or awaiting dehum/data

    // What the UI shows: the requested value while pending, else dehum/data's.
    // False while neither knows the key.
    bool display_value(int idx, const DehumState& s, int& out) const;
    // All of them, for dehum_range()/dehum_format() (a threshold's range
    // follows the sensor the user just picked, not the one last reported).
    DehumValues display_values(const DehumState& s) const;

    // One-shot notice. False when there is nothing to show.
    bool take_toast(char* out, size_t n);

private:
    struct Entry {
        int  want = 0;
        int  pre = 0;            // reported value when published, for the clear rule
        bool pre_known = false;
        uint32_t queued_ms = 0;
        uint32_t sent_ms = 0;
        bool active = false;
        bool sent = false;
        bool resend = false;     // re-edited while an earlier value was pending
    };
    Entry e_[DEHUM_NKEYS];
    char toast_[96] = {0};
    bool has_toast_ = false;

    void edit(int idx, int value, uint32_t now_ms);
    void toast(const char* msg);
};
