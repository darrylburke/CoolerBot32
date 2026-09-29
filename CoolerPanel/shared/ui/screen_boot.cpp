#include "lvgl.h"
#include "theme.h"
#include <cstdint>

// Boot splash, mirrors mockups/00_boot_splash.png / display_design.md §5.1:
// "Cooler32" wordmark (snowflake + "Cooler", "32" in the ice-blue accent) + firmware
// version, three connectivity progress lines, and a "CREATED BY" caption
// over the NorthTrail.AI logo in a white rounded badge anchored to the
// bottom. Replaced by the dashboard on first render (main.cpp).

// Set when the splash's gear button is tapped; the platform's boot flow
// polls this (while pumping lv_timer_handler) to enter setup mode. Cleared
// on every boot_build. Replaces relying solely on the boot touch-hold
// gesture, which can't work from a cold power-on (the touch controller
// calibrates a resting finger into its baseline at power-up).
static bool s_setup_requested = false;
bool boot_setup_requested() { return s_setup_requested; }

static void status_row(lv_obj_t* root, int y, const char* label, const char* value, lv_color_t val_color) {
    lv_obj_t* l = lv_label_create(root);
    lv_obj_set_style_text_font(l, theme_fonts().body, 0);
    lv_obj_set_style_text_color(l, th_muted(), 0);
    lv_obj_set_pos(l, 150, y);
    lv_label_set_text(l, label);

    lv_obj_t* v = lv_label_create(root);
    lv_obj_set_style_text_font(v, theme_fonts().body, 0);
    lv_obj_set_style_text_color(v, val_color, 0);
    lv_obj_set_width(v, 150);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(v, 180, y);
    lv_label_set_text(v, value);
}

lv_obj_t* boot_build(lv_obj_t* parent, const lv_image_dsc_t* wordmark,
                     const lv_image_dsc_t* logo) {
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, 480, 480); lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, th_page(), 0);
    lv_obj_set_style_border_width(root, 0, 0); lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    s_setup_requested = false;

    // Setup gear, top-right: tap during the splash to enter the portal.
    lv_obj_t* gear = lv_obj_create(root);
    lv_obj_set_size(gear, 64, 64); lv_obj_set_pos(gear, 480 - 64 - 8, 8);
    lv_obj_set_style_bg_color(gear, th_surface(), 0);
    lv_obj_set_style_radius(gear, 12, 0);
    lv_obj_set_style_border_width(gear, 0, 0);
    lv_obj_remove_flag(gear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gear, [](lv_event_t*) { s_setup_requested = true; },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_t* gi = lv_label_create(gear);
    lv_label_set_text(gi, LV_SYMBOL_SETTINGS);   // built-in montserrat symbol
    lv_obj_set_style_text_font(gi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gi, th_muted(), 0);
    lv_obj_align(gi, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t* gt = lv_label_create(gear);
    lv_label_set_text(gt, "SETUP");
    lv_obj_set_style_text_font(gt, theme_fonts().overline, 0);
    lv_obj_set_style_text_color(gt, th_muted(), 0);
    lv_obj_align(gt, LV_ALIGN_CENTER, 0, 12);

    if (wordmark) {
        // Brand wordmark image (assets/brand/cooler32-wordmark-dark.png), scaled
        // to ~64px tall and centred — same pivot-centred placement trick as
        // the creator logo below.
        lv_obj_t* img = lv_image_create(root);
        lv_image_set_src(img, wordmark);
        lv_image_set_antialias(img, true);
        int target_h = 64;
        int src_h = (int)wordmark->header.h, src_w = (int)wordmark->header.w;
        uint32_t zoom = (uint32_t)((target_h * 256 + src_h / 2) / src_h);
        lv_image_set_scale(img, zoom);
        lv_obj_set_pos(img, 240 - src_w / 2, 66 - src_h / 2);
    } else {
        // Text fallback: "Cooler" + accent "32" as one recoloured label
        // (LVGL's "#RRGGBB text#" inline markup; #78c8ff = the wordmark's blue).
        lv_obj_t* word = lv_label_create(root);
        lv_obj_set_style_text_font(word, theme_fonts().hero_lg, 0);
        lv_obj_set_style_text_color(word, th_ink(), 0);
        lv_label_set_recolor(word, true);
        lv_obj_set_width(word, 480);
        lv_obj_set_style_text_align(word, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(word, 0, 50);
        lv_label_set_text(word, "Cooler#78c8ff 32#");
    }

    lv_obj_t* ver = lv_label_create(root);
    lv_obj_set_style_text_font(ver, theme_fonts().num_sm, 0);
    lv_obj_set_style_text_color(ver, th_muted(), 0);
    lv_obj_set_width(ver, 480);
    lv_obj_set_style_text_align(ver, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ver, 0, 104);
    lv_label_set_text(ver, "v1.0.0");

    // Progress lines. The sim has no real WiFi/NTP state machine, so these
    // mirror mockups/00_boot_splash.png's literal snapshot (WiFi/NTP already
    // resolved, MQTT still connecting) rather than driving off live state.
    status_row(root, 165, "Wi-Fi", "ok", th_good());
    status_row(root, 197, "NTP", "ok", th_good());
    status_row(root, 229, "MQTT", "connecting...", th_warn());

    lv_obj_t* cb = lv_label_create(root);
    lv_obj_set_style_text_font(cb, theme_fonts().overline, 0);
    lv_obj_set_style_text_color(cb, th_muted(), 0);
    lv_obj_set_width(cb, 480);
    lv_obj_set_style_text_align(cb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cb, 0, 325);
    lv_label_set_text(cb, "CREATED BY");

    // White rounded badge (display_design.md §5.1: 14px radius, logo ~72px
    // tall, badge ~345x104), anchored near the bottom.
    lv_obj_t* badge = lv_obj_create(root);
    lv_obj_set_size(badge, 345, 104); lv_obj_set_pos(badge, 68, 352);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(badge, 14, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    if (logo) {
        // Sibling of the badge, not a child: the raw dsc is the logo's
        // *native* resolution (1042x252 for mockups/northtrail_logo.png),
        // far bigger than the badge, and parenting it under the badge would
        // clip the (unscaled) widget box. lv_image scaling is centred on the
        // object's own pivot (default 50/50%), so placing the *unscaled* box
        // centred on the badge's centre keeps the *scaled* render centred.
        lv_obj_t* img = lv_image_create(root);
        lv_image_set_src(img, logo);
        lv_image_set_antialias(img, true);
        int target_h = 72;
        int src_h = (int)logo->header.h, src_w = (int)logo->header.w;
        uint32_t zoom = (uint32_t)((target_h * 256 + src_h / 2) / src_h);
        lv_image_set_scale(img, zoom);
        int badge_cx = 68 + 345 / 2, badge_cy = 352 + 104 / 2;
        lv_obj_set_pos(img, badge_cx - src_w / 2, badge_cy - src_h / 2);
    }

    return root;
}
