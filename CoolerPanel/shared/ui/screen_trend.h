#pragma once
#include "lvgl.h"

lv_obj_t* screen_trend_create(lv_obj_t* parent);
void screen_trend_refresh(void);
void trend_set_zoom(int seconds);   // 3600 | 86400 | 604800
int  trend_zoom(void);
