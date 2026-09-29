#include "lvgl.h"
#include "theme.h"
#include <cstdio>
#include <cstring>

// Setup-mode (captive portal) screen: large-type AP credentials + URL and
// two tap-to-show fullscreen QR codes (join Wi-Fi / open the setup page).
// Shared so the simulator can render it (--screen setup) and the device
// portal shows the identical screen.

static char s_qr_wifi[160];
static char s_qr_url[32];

struct QrSpec { const char* caption; const char* data; };
static QrSpec s_spec_wifi = {"Scan to join the Wi-Fi", s_qr_wifi};
static QrSpec s_spec_url  = {"Scan to open the setup page", s_qr_url};

static void show_qr_overlay(QrSpec* spec) {
    lv_obj_t* ov = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ov, 480, 480);
    lv_obj_set_style_bg_color(ov, lv_color_white(), 0);
    lv_obj_set_style_radius(ov, 0, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* qr = lv_qrcode_create(ov);
    lv_qrcode_set_size(qr, 320);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, spec->data, strlen(spec->data));
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t* cap = lv_label_create(ov);
    lv_label_set_text(cap, spec->caption);
    lv_obj_set_style_text_font(cap, theme_fonts().title, 0);
    lv_obj_set_style_text_color(cap, lv_color_black(), 0);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -64);

    lv_obj_t* close = lv_label_create(ov);
    lv_label_set_text(close, "tap anywhere to close");
    lv_obj_set_style_text_font(close, theme_fonts().body, 0);
    lv_obj_set_style_text_color(close, lv_color_hex(0x888888), 0);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_add_event_cb(ov, [](lv_event_t* ev) {
        lv_obj_delete((lv_obj_t*)lv_event_get_current_target(ev));
    }, LV_EVENT_CLICKED, nullptr);
}

static void show_qr(lv_event_t* e) {
    show_qr_overlay((QrSpec*)lv_event_get_user_data(e));
}

// Simulator screenshot hook: open the join-Wi-Fi QR overlay without a tap.
void setup_show_wifi_qr() {
    show_qr_overlay(&s_spec_wifi);
}

static void qr_button(lv_obj_t* card, const char* text, QrSpec* spec, lv_align_t align) {
    lv_obj_t* btn = lv_button_create(card);
    lv_obj_set_size(btn, 194, 72);
    lv_obj_set_style_bg_color(btn, th_page(), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_align(btn, align, 0, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, theme_fonts().title, 0);
    lv_obj_set_style_text_color(lbl, th_ink(), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, show_qr, LV_EVENT_CLICKED, spec);
}

void setup_build(lv_obj_t* scr, const char* ap_ssid, const char* ap_pass) {
    snprintf(s_qr_wifi, sizeof s_qr_wifi, "WIFI:T:WPA;S:%s;P:%s;;", ap_ssid, ap_pass);
    snprintf(s_qr_url, sizeof s_qr_url, "http://192.168.4.1");

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, th_page(), 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Setup mode");
    lv_obj_set_style_text_font(title, theme_fonts().hero, 0);
    lv_obj_set_style_text_color(title, th_ink(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_set_size(card, 448, 400);
    lv_obj_set_style_bg_color(card, th_surface(), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -14);

    auto row = [&](int y, const char* k, const char* v, const lv_font_t* vf) {
        lv_obj_t* lk = lv_label_create(card);
        lv_label_set_text(lk, k);
        lv_obj_set_style_text_font(lk, theme_fonts().body, 0);
        lv_obj_set_style_text_color(lk, th_muted(), 0);
        lv_obj_set_pos(lk, 0, y);
        lv_obj_t* lv_ = lv_label_create(card);
        lv_label_set_text(lv_, v);
        lv_obj_set_style_text_font(lv_, vf, 0);
        lv_obj_set_style_text_color(lv_, th_ink(), 0);
        lv_obj_set_pos(lv_, 0, y + 18);
    };
    row(0,   "1. JOIN WI-FI NETWORK", ap_ssid,              theme_fonts().hero);
    row(78,  "2. PASSWORD",           ap_pass,              theme_fonts().hero);
    row(156, "3. OPEN IN A BROWSER",  "http://192.168.4.1", theme_fonts().title);

    qr_button(card, "QR:\njoin Wi-Fi", &s_spec_wifi, LV_ALIGN_BOTTOM_LEFT);
    qr_button(card, "QR:\nsetup page", &s_spec_url,  LV_ALIGN_BOTTOM_RIGHT);
}
