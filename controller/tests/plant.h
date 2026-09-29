// Crude walk-in + Frigidaire model for scenario sims. One tick = 1 s.
//
//  - The AC turns the controller's relay request into compressor state with
//    its own restart delay and minimum on-time.
//  - A running compressor pulls the coil toward (box - 10 C); the coil ices
//    below 0 C in reality -- the sims assert the controller never lets the
//    relay stay closed there.
//  - With the compressor off, the continuous fan pulls the coil to box temp.
//  - The box leaks toward ambient and is cooled by the coil.
#pragma once
#include <cstdint>

namespace th {

struct Plant {
    float box = 20.0f, fin = 20.0f, ambient = 22.0f;
    bool comp = false;
    uint32_t comp_changed_ms = 0;
    uint32_t restart_delay_ms = 180000;
    uint32_t min_on_ms = 0;
    bool unplugged = false;

    static constexpr float COIL_DT = 10.0f;       // coil runs this far below box
    static constexpr float TAU_COIL_ON = 300.0f;  // s, coil + ice thermal mass
    static constexpr float TAU_COIL_OFF = 60.0f;  // s
    static constexpr float TAU_LEAK = 72000.0f;   // s, box -> ambient
    static constexpr float TAU_COOL = 3000.0f;    // s, coil -> box

    void tick(bool relay, uint32_t now) {
        if (unplugged) {
            comp = false;
        } else if (!comp && relay && now - comp_changed_ms >= restart_delay_ms) {
            comp = true; comp_changed_ms = now;
        } else if (comp && !relay && now - comp_changed_ms >= min_on_ms) {
            comp = false; comp_changed_ms = now;
        }
        float target = comp ? box - COIL_DT : box;
        fin += (target - fin) / (comp ? TAU_COIL_ON : TAU_COIL_OFF);
        float cool = comp ? (box - fin) / TAU_COOL : 0.0f;
        box += (ambient - box) / TAU_LEAK - cool;
    }
};

}  // namespace th
