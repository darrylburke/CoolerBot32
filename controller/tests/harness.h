// Test helpers shared by the unit tests and the scenario sims.
#pragma once
#include "cooler_logic.h"

namespace th {

// ADC volts the divider produces for a thermistor at temp_c (Beta 3950, 10k).
inline float volts_for_c(float temp_c, float beta = 3950.0f, float r0 = 10000.0f) {
    float r = r0 * std::exp(beta * (1.0f / (temp_c + 273.15f) - 1.0f / cooler::T0_K));
    return cooler::FIN_VREF * r / (r + cooler::FIN_R_FIXED);
}

// Owns one controller instance and advances it one second per tick().
struct Rig {
    cooler::Settings s;
    cooler::FinCal cal;
    cooler::LogicState st;
    cooler::Inputs in;
    cooler::Outputs out;
    uint32_t now = 0;
    uint32_t step_ms = 1000;   // tick length; real ESPHome intervals can run late

    Rig() {
        in.box_valid = true; in.box_c = 4.0f;
        in.fin_volts = volts_for_c(4.0f);
        in.mqtt_connected = true;
    }
    void box(float c) { in.box_valid = true; in.box_c = c; }
    void box_dead() { in.box_valid = false; in.box_c = NAN; }
    void fin(float c) { in.fin_volts = volts_for_c(c); }
    void fin_open() { in.fin_volts = 3.29f; }
    const cooler::Outputs& tick() { out = cooler::step(in, s, cal, st, now); now += step_ms; return out; }
    const cooler::Outputs& run(uint32_t seconds) { for (uint32_t i = 0; i < seconds; i++) tick(); return out; }
};

}  // namespace th
