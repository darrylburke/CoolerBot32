#pragma once
#include "lvgl.h"
void nav_init(lv_obj_t* root);
void nav_refresh(void);
// Jump straight to a page: 0 Trend, 1 Settings, 2 Detail. No animation.
void nav_show_page(int index);
