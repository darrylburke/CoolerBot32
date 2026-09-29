#pragma once
#include <stddef.h>
#include <stdint.h>
struct CoolerState;

// Plain-text renderings of the controller's run state, shared by the Trend
// and Detail screens. Pure functions (no LVGL) so they are unit-tested.

// "45s" / "12m" / "1h 05m".
const char* fmt_dur(uint32_t secs, char* out, size_t cap);

// Human mode label: "NORMAL", "OVERRIDE", "FIN PROXY", "OVR + FIN PROXY",
// "BLIND TIMER"; "--" before any /data.
const char* fmt_mode_label(const CoolerState& s);

// The status card's big line: "Cooling 2m", "Cooling • min run 38s",
// "Defrost 1m", "Waiting 3m", "Resting 6m", "Fan only 15m"; "--" before any /data.
const char* fmt_state_value(const CoolerState& s, char* out, size_t cap);

// The status card's key line: the mode label, plus what a rest is waiting on
// ("FIN PROXY - SETTLE", "NORMAL - BACKUP DUTY").
const char* fmt_state_key(const CoolerState& s, char* out, size_t cap);

// Compressor card: "Running", "Starting" (relay closed, compressor not yet
// seen), "Stopped", or "--" when the fin sensor cannot tell.
const char* fmt_compressor(const CoolerState& s);

// Fin calibration summary: "fin cal ok", "fin uncalibrated",
// "calibrating 3 pts 6.2C".
const char* fmt_fincal(const CoolerState& s, char* out, size_t cap);

// Header chip for the override switch, derived from /data's override_src:
// "SWITCH ON" (switch or both), "SWITCH OFF" (none), "LINK LOST" (link: override
// forced by the controller losing the broker), "--" before any /data.
// alert = draw it in the warning colour.
struct SwitchChip { const char* text; bool alert; };
SwitchChip fmt_switch_chip(const CoolerState& s);
