#pragma once
#include "lvgl.h"

// The fourth page: the dehumidifier's temperature and humidity (Graph) and
// its mode, settings and AUTO rules (Controls). Spec 2026-10-31 §3.
lv_obj_t* screen_dehum_create(lv_obj_t* parent);
void screen_dehum_refresh(void);
void screen_dehum_show_tab(int tab);    // 0 Graph, 1 Controls
void screen_dehum_scroll_to(int y);     // Controls: scroll its page to y px
