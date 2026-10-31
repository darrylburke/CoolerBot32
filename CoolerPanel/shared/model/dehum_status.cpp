#include "dehum_status.h"
#include "dehum_state.h"
#include <cstdio>
#include <cstring>

// Minutes rounded up; hours once there is an hour or more.
static void left(uint32_t remain_s, char* out, size_t cap) {
    const uint32_t min = (remain_s + 59) / 60;
    const uint32_t h = min / 60, m = min % 60;
    if (h == 0)      std::snprintf(out, cap, "%u m left", (unsigned)m);
    else if (m == 0) std::snprintf(out, cap, "%u h left", (unsigned)h);
    else             std::snprintf(out, cap, "%u h %u m left", (unsigned)h, (unsigned)m);
}

const char* dehum_mode_text(const DehumState& s, char* out, size_t cap) {
    char l[24];
    if (!s.valid || s.mode == DehumMode::Unknown) std::snprintf(out, cap, "--");
    else if (s.mode == DehumMode::Timer) {
        left(s.remain_s, l, sizeof l);
        std::snprintf(out, cap, "TIMER %s", l);
    } else if (s.mode == DehumMode::Cycle) {
        left(s.remain_s, l, sizeof l);
        std::snprintf(out, cap, "CYCLE %s %s", s.phase == 0 ? "rest" : "run", l);
    } else {
        std::snprintf(out, cap, "%s", dehum_mode_label(s.mode));
    }
    return out;
}

const char* dehum_auto_text(const DehumState& s, char* out, size_t cap) {
    if (cap) out[0] = 0;
    if (!s.valid || s.mode != DehumMode::Auto) return out;
    char list[24] = "";
    size_t used = 0;
    for (int i = 0; i < DEHUM_RULES; i++) {
        if (!s.rule_match[i]) continue;
        used += std::snprintf(list + used, sizeof list - used, used ? ", %d" : "%d", i + 1);
    }
    if (used) std::snprintf(out, cap, "rules met: %s", list);
    else      std::snprintf(out, cap, "no rule met");
    return out;
}

const char* dehum_relays_text(const DehumState& s, char* out, size_t cap) {
    if (!s.valid) { if (cap) out[0] = 0; return out; }
    std::snprintf(out, cap, "Relay 1 %s • Relay 2 %s", s.relay ? "on" : "off", s.relay2 ? "on" : "off");
    return out;
}

const char* dehum_status_line(const DehumState& s, char* out, size_t cap) {
    char a[40], b[40], c[40];
    const char* parts[3] = {dehum_mode_text(s, a, sizeof a), dehum_relays_text(s, b, sizeof b),
                            dehum_auto_text(s, c, sizeof c)};
    size_t used = 0;
    if (cap) out[0] = 0;
    for (const char* p : parts) {
        if (!*p || used >= cap) continue;
        used += std::snprintf(out + used, cap - used, used ? "  •  %s" : "%s", p);
    }
    return out;
}

DehumView dehum_health(link_state_t link, const DehumState& s, int64_t now_epoch) {
    if (link != LINK_OK) return DehumView::BrokerDown;
    if (s.availability_seen && !s.online) return DehumView::Offline;
    if (!s.valid) return DehumView::Waiting;
    if (now_epoch - s.last_rx_epoch > DEHUM_SILENT_AFTER_S) return DehumView::Silent;
    return DehumView::Online;
}

const char* dehum_view_text(DehumView v) {
    switch (v) {
    case DehumView::BrokerDown: return "Broker unreachable";
    case DehumView::Offline:    return "Dehumidifier offline";
    case DehumView::Waiting:    return "Waiting for dehumidifier";
    case DehumView::Silent:     return "Dehumidifier silent: no data for 90 s";
    case DehumView::Online:     return "Online";
    }
    return "";
}
