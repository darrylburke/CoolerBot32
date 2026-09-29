#include "alarm.h"
#include "cooler_state.h"

void Alarms::update(const CoolerState& s, int64_t now_epoch) {
    // Marks a condition live or clear. `since` records when it first became
    // true, so sustained-duration conditions can measure against it. Clearing
    // also drops any acknowledge, so a condition that comes back is a fresh
    // alarm rather than one still inside an old hold-off.
    auto mark = [&](AlarmId id, bool live) {
        Cond& c = conds_[(int)id];
        if (live && !c.live) c.since = now_epoch;
        if (!live) { c.since = 0; c.ack_until = 0; }
        c.live = live;
    };

    // Gated on availability_seen, NOT valid: the retained LWT can report a
    // dead cooler before /data has ever parsed successfully, and that is
    // exactly the case this alarm must not stay silent for.
    mark(AlarmId::ControllerOffline, s.availability_seen && !s.online);
    mark(AlarmId::ControllerSilent,
         s.valid && (now_epoch - s.last_rx_epoch) > cfg_.silent_s);
    mark(AlarmId::NoResponse, s.valid && s.no_response != 0);
    mark(AlarmId::BoxSensorFault, s.valid && s.sht_fault != 0);
    mark(AlarmId::FinSensorFault, s.valid && s.fin_fault != 0);
    // Measured against the band actually in force: in override the
    // controller holds its fixed 3..5 C, not coolerset +/- range. A null
    // temperature is BoxSensorFault's business, never "not keeping up".
    const float band_hi = cooler_in_override(s) ? 5.0f : (float)(s.coolerset + s.range);
    // A temperature is only known if it is valid AND recent: after silent_s
    // with no /data the last number is stale, so NotKeepingUp pauses rather
    // than firing on it once ControllerSilent has been acknowledged.
    const bool fresh = s.valid && (now_epoch - s.last_rx_epoch) <= cfg_.silent_s;
    temp_known_ = s.temp_valid && fresh;
    if (!s.valid)
        mark(AlarmId::NotKeepingUp, false);
    else if (temp_known_)
        mark(AlarmId::NotKeepingUp, s.temp > band_hi + (float)cfg_.over_c);
    // else: temp unknown -- leave the condition (and its `since`) untouched.
}

AlarmId Alarms::active(int64_t now_epoch) const {
    // Iterates in enum order, which is priority order -- ControllerOffline
    // outranks NotKeepingUp.
    for (int i = 1; i < (int)AlarmId::COUNT; i++) {
        const Cond& c = conds_[i];
        if (!c.live) continue;
        // NotKeepingUp must have held continuously for over_s. The others
        // fire the moment they go true.
        if ((AlarmId)i == AlarmId::NotKeepingUp &&
            (!temp_known_ || (now_epoch - c.since) <= cfg_.over_s)) continue;
        // Suppressed only while still inside the hold-off window.
        if (now_epoch < c.ack_until) continue;
        return (AlarmId)i;
    }
    return AlarmId::None;
}

void Alarms::acknowledge(AlarmId id, int64_t now_epoch) {
    if (id == AlarmId::None || id >= AlarmId::COUNT) return;
    conds_[(int)id].ack_until = now_epoch + cfg_.holdoff_s;
}

bool Alarms::any_latched() const {
    for (int i = 1; i < (int)AlarmId::COUNT; i++)
        if (conds_[i].live) return true;
    return false;
}

const char* Alarms::text(AlarmId id) {
    switch (id) {
        case AlarmId::ControllerOffline: return "COOLER OFFLINE";
        case AlarmId::ControllerSilent:  return "NO DATA";
        case AlarmId::NoResponse:        return "AC NOT RESPONDING";
        case AlarmId::BoxSensorFault:    return "BOX SENSOR FAULT";
        case AlarmId::FinSensorFault:    return "COIL SENSOR FAULT";
        case AlarmId::NotKeepingUp:      return "NOT COOLING";
        default:                         return "";
    }
}
