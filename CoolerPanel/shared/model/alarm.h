#pragma once
#include <stdint.h>
struct CoolerState;

// Ordered by priority -- lower value wins when several are live at once.
enum class AlarmId { None = 0, ControllerOffline, ControllerSilent,
                     NoResponse, BoxSensorFault, FinSensorFault,
                     NotKeepingUp, COUNT };

// NotKeepingUp's defaults are calibrated against THIS cooler's measured
// behaviour, not intuition. Its own published hourly history for a normal
// pull-down was [16,14,13,11,10,9,9,9,8,8,8,8,8,8,9,9] degC against a
// setpoint of 5 -- i.e. a walk-in this size takes many hours to come down
// from ambient and legitimately sits well above setpoint for several of
// them, then holds steady around 8-9.
//
// The original 5 degC / 60 min would therefore have fired on the very first
// boot and re-fired every 30 minutes through every door opening, warm load
// and controller restart. An alarm that cries wolf on normal operation is
// worse than no alarm, because it trains the operator to dismiss the one
// screen that matters.
//
// 8 degC over the band (a 14 degC trip point at the current setpoint) sits
// above everything except the first hour or two of a cold start, and 6 hours
// sustained is longer than any legitimate pull-down observed. A genuine
// failure parks the box near ambient and stays there, so it still trips.
struct AlarmCfg {
    int silent_s  = 300;     // no /data for this long -> ControllerSilent
    int over_c    = 8;       // degrees above coolerset+range
    int over_s    = 21600;   // ...sustained this long (6 h) -> NotKeepingUp
    int holdoff_s = 1800;    // acknowledge suppression window
};

class Alarms {
public:
    void configure(const AlarmCfg& c) { cfg_ = c; }
    void update(const CoolerState& s, int64_t now_epoch);

    // Highest-priority condition that is live, has held long enough, and is
    // not inside its acknowledge hold-off. Needs the clock, so it takes one.
    AlarmId active(int64_t now_epoch) const;

    void acknowledge(AlarmId id, int64_t now_epoch);

    // True if any condition is currently live, acknowledged or not.
    bool any_latched() const;

    static const char* text(AlarmId id);

private:
    struct Cond {
        bool live = false;
        int64_t since = 0;       // when it first became true
        int64_t ack_until = 0;   // suppressed while now < this
    };
    AlarmCfg cfg_;
    Cond conds_[(int)AlarmId::COUNT];
    // False while the controller publishes temp:null. NotKeepingUp is then
    // paused -- neither fired nor reset -- so a glitch can't restart its 6 h.
    bool temp_known_ = true;
};
