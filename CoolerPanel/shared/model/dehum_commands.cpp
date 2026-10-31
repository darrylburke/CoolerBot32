#include "dehum_commands.h"
#include "platform.h"
#include <cstdio>
#include <cstring>

static const char* const kNotSent = "Couldn't send: dehumidifier not reachable";

static bool publish(const char* payload, int n) {
    return n > 0 && platform_mqtt_publish(DEHUM_TOPIC_CMD, payload, (size_t)n, false);
}

void DehumCommands::toast(const char* msg) {
    std::snprintf(toast_, sizeof toast_, "%s", msg);
    has_toast_ = true;
}

bool DehumCommands::set_mode(DehumMode m, bool enabled) {
    const char* w = dehum_mode_wire(m);
    char payload[32];
    const int n = std::snprintf(payload, sizeof payload, "{\"mode\":\"%s\"}", w);
    if (!*w || !enabled || !publish(payload, n)) { toast(kNotSent); return false; }
    return true;
}

void DehumCommands::edit(int idx, int value, uint32_t now_ms) {
    Entry& e = e_[idx];
    // Re-editing a value already on its way: it must be sent even if it lands
    // back on what dehum/data last said, or the first request would stand.
    e.resend = e.active && (e.sent || e.resend);
    e.want = value;
    e.queued_ms = now_ms;
    e.sent = false;
    e.active = true;
}

bool DehumCommands::step(int idx, int dir, const DehumState& s, bool enabled, uint32_t now_ms) {
    if (idx < 0 || idx >= DEHUM_NKEYS || !enabled) return false;
    const DehumValues cur = display_values(s);
    if (!cur.known[idx]) return false;
    int lo, hi;
    if (!dehum_range(idx, cur, lo, hi)) return false;
    int next = cur.v[idx] + dir * dehum_step(idx);
    if (next < lo) next = lo;
    if (next > hi) next = hi;
    if (next == cur.v[idx]) return false;
    edit(idx, next, now_ms);
    return true;
}

bool DehumCommands::enter(int idx, int value, const DehumState& s, bool enabled, uint32_t now_ms) {
    if (idx < 0 || idx >= DEHUM_NKEYS || !enabled) return false;
    int lo, hi;
    if (!dehum_range(idx, display_values(s), lo, hi) || value < lo || value > hi) return false;
    edit(idx, value, now_ms);
    return true;
}

void DehumCommands::tick(uint32_t now_ms, const DehumState& s, bool enabled) {
    char expired[96] = "";
    size_t used = 0;
    for (int i = 0; i < DEHUM_NKEYS; i++) {
        Entry& e = e_[i];
        if (!e.active) continue;
        if (!e.sent) {
            if (now_ms - e.queued_ms < DEHUM_DEBOUNCE_MS) continue;
            // Back where dehum/data already has it, with nothing in flight:
            // there is nothing to send (the app drops such a draft too).
            if (!e.resend && s.settings.known[i] && s.settings.v[i] == e.want) {
                e.active = false;
                continue;
            }
            char payload[48];
            const int n = std::snprintf(payload, sizeof payload, "{\"%s\":%d}", dehum_key(i), e.want);
            if (!enabled || !publish(payload, n)) {
                toast(kNotSent);
                e = Entry{};             // revert: the UI falls back to dehum/data
                continue;
            }
            e.pre = s.settings.v[i];
            e.pre_known = s.settings.known[i];
            e.sent = true;
            e.sent_ms = now_ms;
        } else if (now_ms - e.sent_ms >= DEHUM_DEADLINE_MS) {
            char l[32];
            dehum_label(i, l, sizeof l);
            if (used < sizeof expired)
                used += std::snprintf(expired + used, sizeof expired - used, used ? ", %s" : "%s", l);
            e = Entry{};
        }
    }
    if (used) {
        char msg[sizeof toast_];
        std::snprintf(msg, sizeof msg, "Not applied: %s", expired);
        toast(msg);
    }
}

void DehumCommands::on_state(const DehumState& s) {
    for (int i = 0; i < DEHUM_NKEYS; i++) {
        Entry& e = e_[i];
        if (!e.active || !e.sent || !s.settings.known[i]) continue;
        const int reported = s.settings.v[i];
        // dehum/data comes every 30 s whether or not anything changed, so a
        // message is not proof the command landed: clear only when it reflects
        // it -- the value moved off what it was when we sent, or it is what we
        // asked for (a clamp shows up as the former).
        if (reported == e.want || !e.pre_known || reported != e.pre) e = Entry{};
    }
}

bool DehumCommands::is_pending(int idx) const {
    return idx >= 0 && idx < DEHUM_NKEYS && e_[idx].active;
}

bool DehumCommands::display_value(int idx, const DehumState& s, int& out) const {
    if (idx < 0 || idx >= DEHUM_NKEYS) return false;
    if (e_[idx].active) { out = e_[idx].want; return true; }
    if (!s.settings.known[idx]) return false;
    out = s.settings.v[idx];
    return true;
}

DehumValues DehumCommands::display_values(const DehumState& s) const {
    DehumValues d = s.settings;
    for (int i = 0; i < DEHUM_NKEYS; i++)
        if (e_[i].active) { d.v[i] = e_[i].want; d.known[i] = true; }
    return d;
}

bool DehumCommands::take_toast(char* out, size_t n) {
    if (!has_toast_) return false;
    std::snprintf(out, n, "%s", toast_);
    has_toast_ = false;
    return true;
}
