#pragma once
#include "lvgl.h"

// Card icons, generated from assets/icons/*.png by tools/gen_icons.py into
// shared/assets/icons.cpp. They are A8 (alpha-only), so the fill comes from
// the widget's image_recolor style rather than the source art -- that is what
// lets one icon go cyan while its unit runs and grey while it rests.
extern const lv_image_dsc_t icon_aircon;      // AC 1 / AC 2 -- the units themselves
extern const lv_image_dsc_t icon_snowflake;   // In AC       -- actively cooling
extern const lv_image_dsc_t icon_duty;        // In Duty     -- the duty cycle turning over
