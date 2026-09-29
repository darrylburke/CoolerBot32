#pragma once
#include "lvgl.h"
lv_obj_t* screen_settings_create(lv_obj_t* parent);
void screen_settings_refresh(void);
// Select a tab: 0 Box, 1 Coil, 2 Timing. Used by the sim's --tab flag.
void screen_settings_show_tab(int tab);
