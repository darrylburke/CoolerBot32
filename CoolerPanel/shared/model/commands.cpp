#include "commands.h"
#include "cooler_state.h"
#include "bounds.h"
#include "mqtt_router.h"
#include "platform.h"
#include <cstring>
#include <cstdio>

// Reads the live value of `key` out of state. Kept in one place so
// display_value() and on_state() cannot disagree about where a value lives.
static int state_value(const char* key, const CoolerState& s) {
    if (!std::strcmp(key, "coolerset"))      return s.coolerset;
    if (!std::strcmp(key, "range"))          return s.range;
    if (!std::strcmp(key, "fin_cutoff"))     return s.fin_cutoff;
    if (!std::strcmp(key, "fin_recover"))    return s.fin_recover;
    if (!std::strcmp(key, "settle"))         return s.settle;
    if (!std::strcmp(key, "minofftime"))     return s.minofftime;
    if (!std::strcmp(key, "minruntime"))     return s.minruntime;
    if (!std::strcmp(key, "maxrun"))         return s.maxrun;
    if (!std::strcmp(key, "dutypercent"))    return s.dutypercent;
    if (!std::strcmp(key, "sampleinterval")) return s.sampleinterval;
    return 0;
}

Commands::Entry* Commands::find(const char* key) {
    for (auto& e : entries_)
        if (e.active && !std::strcmp(e.key, key)) return &e;
    return nullptr;
}
const Commands::Entry* Commands::find(const char* key) const {
    for (const auto& e : entries_)
        if (e.active && !std::strcmp(e.key, key)) return &e;
    return nullptr;
}
Commands::Entry* Commands::alloc(const char* key) {
    if (Entry* e = find(key)) return e;
    for (auto& e : entries_) {
        if (!e.active) {
            e = Entry{};
            std::strncpy(e.key, key, sizeof(e.key) - 1);
            e.active = true;
            return &e;
        }
    }
    return nullptr;
}

void Commands::set_toast(const char* key) {
    std::snprintf(toast_, sizeof(toast_), "%s not acknowledged", key);
    has_toast_ = true;
}

void Commands::request(const char* key, int value, uint32_t now_ms) {
    Entry* e = alloc(key);
    if (!e) {
        // Table exhausted -- surface it rather than dropping the edit
        // silently. A ninth-plus concurrently-edited key must still be
        // visible as a failure, not vanish with no feedback at all.
        set_toast(key);
        return;
    }
    // pre is captured in tick(), immediately before THIS entry's specific
    // publish -- not here. Capturing it at request() time would let a
    // same-key re-request before the prior command's ack inherit a stale
    // pre (the value from before the FIRST command), which lets an
    // unrelated /data that merely echoes the first command's result get
    // misread as an answer to the second, still-unacknowledged one.
    e->want = bounds_clamp(key, value, last_fin_cutoff_);
    e->queued_ms = now_ms;
    e->sent = false;
}

void Commands::tick(uint32_t now_ms) {
    for (auto& e : entries_) {
        if (!e.active) continue;
        if (!e.sent) {
            if (now_ms - e.queued_ms < CMD_DEBOUNCE_MS) continue;
            e.pre = state_value(e.key, last_);   // value right before this publish
            char payload[64];
            int n = std::snprintf(payload, sizeof(payload), "{\"%s\":%d}", e.key, e.want);
            char topic[96];
            std::snprintf(topic, sizeof(topic), "%s/cmd", router_prefix());
            if (!platform_mqtt_publish(topic, payload, (size_t)n, false)) {
                set_toast(e.key);
                e.active = false;       // revert -- UI falls back to state
                continue;
            }
            e.sent = true;
            e.sent_ms = now_ms;
        } else if (now_ms - e.sent_ms > CMD_DEADLINE_MS) {
            set_toast(e.key);
            e.active = false;           // revert
        }
    }
}

void Commands::on_state(const CoolerState& s) {
    last_fin_cutoff_ = s.fin_cutoff;
    for (auto& e : entries_) {
        if (!e.active || !e.sent) continue;
        const int reported = state_value(e.key, s);
        // The cooler publishes /data every 30 s whether or not anything
        // changed, so "a message arrived" is NOT proof our command landed.
        // Clear only when this payload actually reflects it: the value moved
        // off what it was when we published, or it already equals what we
        // asked for. Otherwise stay pending and let the deadline fire --
        // without this, a dropped command reads as success.
        //
        // Clamping is still handled: we ask 50, the cooler answers 40, that
        // differs from pre, so we clear and display 40.
        if (reported != e.pre || reported == e.want) e.active = false;
    }
    last_ = s;
}

bool Commands::action(const char* key, int value) {
    char payload[64];
    int n = std::snprintf(payload, sizeof(payload), "{\"%s\":%d}", key, value);
    char topic[96];
    std::snprintf(topic, sizeof(topic), "%s/cmd", router_prefix());
    if (!platform_mqtt_publish(topic, payload, (size_t)n, false)) {
        std::snprintf(toast_, sizeof(toast_), "%s failed", key);
        has_toast_ = true;
        return false;
    }
    return true;
}

bool Commands::is_pending(const char* key) const {
    const Entry* e = find(key);
    return e != nullptr;
}

int Commands::display_value(const char* key, const CoolerState& s) const {
    if (const Entry* e = find(key)) return e->want;
    return state_value(key, s);
}

bool Commands::take_toast(char* out, size_t n) {
    if (!has_toast_) return false;
    std::strncpy(out, toast_, n - 1);
    out[n - 1] = 0;
    has_toast_ = false;
    return true;
}
