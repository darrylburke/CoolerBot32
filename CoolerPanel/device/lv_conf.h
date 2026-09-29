/* LVGL config for the ESP32-S3 device build. The sim's config lives in
 * firmware/shared/lv_conf.h and enables SDL + FreeType, which don't exist
 * here; this one is referenced via LV_CONF_PATH in platformio.ini. */
#ifndef LV_CONF_H
#define LV_CONF_H
/* LVGL assembles its .S sources against this header too — keep it asm-safe */
#ifndef __ASSEMBLER__
#include <stdint.h>
#endif
#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING   LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_CLIB
#define LV_USE_ARC 1
#define LV_USE_LABEL 1
#define LV_USE_IMAGE 1
#define LV_USE_LINE 1
#define LV_USE_BUTTON 1
#define LV_USE_CANVAS 1
#define LV_USE_QRCODE 1   /* portal setup screen: scan-to-join Wi-Fi QR */
#define LV_USE_TILEVIEW 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#endif
