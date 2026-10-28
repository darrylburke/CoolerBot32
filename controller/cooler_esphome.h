// cooler_esphome.h -- ESPHome-only glue for cooler_logic.h.
//
// ESPHome emits `globals:` declarations *before* it includes project headers,
// so a global cannot have a type from cooler_logic.h. The controller's state
// therefore lives here as function-local statics, and Settings / FinCal are
// persisted through ESPHome's preferences API directly (flash writes are
// batched by ESPHome's flash_write_interval, default 60 s).
#pragma once
#include "esphome/core/preferences.h"
#include "cooler_logic.h"

namespace cooler_glue {

// Preference keys. Changing a struct's layout changes its size, the load
// fails, and the defaults are used -- which is the intended upgrade path.
constexpr uint32_t PREF_SETTINGS = 0xC0015E71u;
constexpr uint32_t PREF_FINCAL = 0xC0017CA1u;

inline cooler::Settings& settings() { static cooler::Settings v; return v; }
inline cooler::FinCal& fincal() { static cooler::FinCal v; return v; }
inline cooler::LogicState& logic() { static cooler::LogicState v; return v; }
inline cooler::Outputs& out() { static cooler::Outputs v; return v; }

inline esphome::ESPPreferenceObject& settings_pref() {
    static esphome::ESPPreferenceObject p =
        esphome::global_preferences->make_preference<cooler::Settings>(PREF_SETTINGS);
    return p;
}
inline esphome::ESPPreferenceObject& fincal_pref() {
    static esphome::ESPPreferenceObject p =
        esphome::global_preferences->make_preference<cooler::FinCal>(PREF_FINCAL);
    return p;
}

// Call once from on_boot. Restores, re-clamps (bounds may have changed
// since the values were stored), and writes back.
inline void load() {
    if (!settings_pref().load(&settings())) settings() = cooler::Settings{};
    cooler::clamp_settings(settings());
    settings_pref().save(&settings());
    if (!fincal_pref().load(&fincal())) fincal() = cooler::FinCal{};
}

inline void save_settings() { settings_pref().save(&settings()); }
inline void save_fincal() { fincal_pref().save(&fincal()); }

}  // namespace cooler_glue
