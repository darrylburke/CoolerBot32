// dehum_esphome.h -- ESPHome-only glue for dehum_logic.h.
//
// ESPHome emits `globals:` before it includes project headers, so the
// logic's types live here as function-local statics, and Settings is
// persisted through ESPHome's preferences API directly.
#pragma once
#include "esphome/core/preferences.h"
#include "dehum_logic.h"

namespace dehum_glue {

// Changing Settings' layout changes its size, the load fails, and the
// defaults are used -- the intended upgrade path.
constexpr uint32_t PREF_DEHUM_SETTINGS = 0xDE40015Eu;

inline dehum::Settings& settings() { static dehum::Settings v; return v; }
inline dehum::State& state() { static dehum::State v; return v; }
inline dehum::Outputs& out() { static dehum::Outputs v; return v; }

inline esphome::ESPPreferenceObject& settings_pref() {
    static esphome::ESPPreferenceObject p =
        esphome::global_preferences->make_preference<dehum::Settings>(PREF_DEHUM_SETTINGS);
    return p;
}

// Call once from on_boot. Restores, re-clamps, and writes back.
inline void load() {
    if (!settings_pref().load(&settings())) settings() = dehum::Settings{};
    dehum::clamp_settings(settings());
    settings_pref().save(&settings());
}

inline void save_settings() { settings_pref().save(&settings()); }

}  // namespace dehum_glue
