#include "screen_config.h"
#include "device_config.h"
#include "ui.h"
#include "theme.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// Layout on 480x480. The keyboard is the constraint: it needs roughly half the
// screen, so the field list scrolls behind it rather than being squeezed.
//
// The field list RESIZES rather than living permanently at keyboard height:
// with the keyboard down all seven rows fit without scrolling at all, and
// only when a field takes focus does the list shrink to make room.
//
//   keyboard down          keyboard up
//   0..44   title + Back   0..44   title + Back
//   48..392 all 7 rows     48..234 list (scrolls)
//   398..458 Save/status   240..480 keyboard
// ---------------------------------------------------------------------------
#define ROW_H       44
#define LIST_Y      48
#define LIST_H_FULL 344      // 7 * 44 = 308 rows + padding, no scrolling
#define LIST_H_KB   186      // shrunk so the keyboard has its 240px
#define BTNS_Y      398
#define FIELD_W   300
#define LABEL_W   120

enum { F_SSID = 0, F_WPASS, F_HOST, F_PORT, F_USER, F_MPASS, F_BASE, F_COUNT };

static const char* kLabels[F_COUNT] = {
    "WiFi SSID", "WiFi pass", "MQTT host", "MQTT port",
    "MQTT user", "MQTT pass", "Base topic"
};

static lv_obj_t* s_root;
static lv_obj_t* s_list;
static lv_obj_t* s_kb;
static lv_obj_t* s_btns;
static lv_obj_t* s_status;
static lv_obj_t* s_field[F_COUNT];
static config_save_fn s_saver = nullptr;

void screen_config_set_saver(config_save_fn fn) { s_saver = fn; }
bool screen_config_is_open(void) {
    return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

static void kb_show(lv_obj_t* ta) {
    lv_obj_set_height(s_list, LIST_H_KB);
    lv_obj_scroll_to_view(ta, LV_ANIM_OFF);   // keep the focused field visible
    lv_keyboard_set_textarea(s_kb, ta);
    // Numeric-only for the port: an alpha keyboard there just invites a value
    // that fails validation after you have already tapped Save.
    lv_keyboard_set_mode(s_kb, ta == s_field[F_PORT]
                         ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btns, LV_OBJ_FLAG_HIDDEN);
}

static void kb_hide(void) {
    lv_obj_set_height(s_list, LIST_H_FULL);
    lv_keyboard_set_textarea(s_kb, nullptr);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_btns, LV_OBJ_FLAG_HIDDEN);
}

static void on_field(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) kb_show(ta);
    else if (c == LV_EVENT_DEFOCUSED) kb_hide();
}

static void on_kb(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) kb_hide();
}

static void say(const char* msg, lv_color_t col) {
    lv_label_set_text(s_status, msg);
    lv_obj_set_style_text_color(s_status, col, 0);
}

static void on_save(lv_event_t*) {
    DeviceConfig c;
    c.wifi_ssid = lv_textarea_get_text(s_field[F_SSID]);
    c.wifi_pass = lv_textarea_get_text(s_field[F_WPASS]);
    c.mqtt_host = lv_textarea_get_text(s_field[F_HOST]);
    c.mqtt_user = lv_textarea_get_text(s_field[F_USER]);
    c.mqtt_pass = lv_textarea_get_text(s_field[F_MPASS]);
    c.mqtt_base = lv_textarea_get_text(s_field[F_BASE]);
    c.mqtt_port = (uint16_t)atoi(lv_textarea_get_text(s_field[F_PORT]));
    c.configured = true;

    // Validate BEFORE persisting. Writing a config that cannot connect, then
    // rebooting into it, is how a wall-mounted panel becomes a brick you have
    // to fetch a laptop for.
    ConfigError err = validate_config(c);
    if (!err.ok) { say(err.message.c_str(), th_crit()); return; }
    if (!s_saver) { say("no saver registered", th_crit()); return; }
    if (!s_saver(c)) { say("save failed", th_crit()); return; }
    say("saved", th_good());
}

static void on_close(lv_event_t*) { screen_config_close(); }

lv_obj_t* screen_config_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, th_page(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(s_root);
    lv_label_set_text(title, "WiFi / MQTT setup");
    lv_obj_set_pos(title, 12, 12);
    lv_obj_set_style_text_color(title, th_ink(), 0);

    lv_obj_t* back = lv_button_create(s_root);
    lv_obj_set_size(back, 90, 34);
    lv_obj_set_pos(back, 378, 6);
    lv_obj_set_style_bg_color(back, th_surface(), 0);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_close, LV_EVENT_CLICKED, nullptr);

    s_list = lv_obj_create(s_root);
    lv_obj_set_size(s_list, 480, LIST_H_FULL);
    lv_obj_set_pos(s_list, 0, LIST_Y);
    lv_obj_set_style_bg_color(s_list, th_page(), 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 6, 0);

    for (int i = 0; i < F_COUNT; i++) {
        lv_obj_t* l = lv_label_create(s_list);
        lv_label_set_text(l, kLabels[i]);
        lv_obj_set_pos(l, 4, i * ROW_H + 10);
        lv_obj_set_style_text_color(l, th_ink2(), 0);

        s_field[i] = lv_textarea_create(s_list);
        lv_obj_set_size(s_field[i], FIELD_W, 36);
        lv_obj_set_pos(s_field[i], LABEL_W, i * ROW_H);
        lv_textarea_set_one_line(s_field[i], true);
        lv_obj_set_style_bg_color(s_field[i], th_surface(), 0);
        lv_obj_set_style_text_color(s_field[i], th_ink(), 0);
        // Both passwords are masked. Someone standing at the cooler should not
        // be able to read the broker credential off the wall.
        if (i == F_WPASS || i == F_MPASS) lv_textarea_set_password_mode(s_field[i], true);
        lv_obj_add_event_cb(s_field[i], on_field, LV_EVENT_ALL, nullptr);
    }

    s_btns = lv_obj_create(s_root);
    lv_obj_set_size(s_btns, 480, 60);
    lv_obj_set_pos(s_btns, 0, BTNS_Y);
    lv_obj_set_style_bg_color(s_btns, th_page(), 0);
    lv_obj_set_style_border_width(s_btns, 0, 0);
    lv_obj_clear_flag(s_btns, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save = lv_button_create(s_btns);
    lv_obj_set_size(save, 150, 44);
    lv_obj_set_pos(save, 12, 4);
    lv_obj_set_style_bg_color(save, th_good(), 0);
    lv_obj_t* sl = lv_label_create(save);
    lv_label_set_text(sl, "Save");
    lv_obj_center(sl);
    lv_obj_add_event_cb(save, on_save, LV_EVENT_CLICKED, nullptr);

    s_status = lv_label_create(s_btns);
    lv_obj_set_pos(s_status, 178, 16);
    lv_obj_set_style_text_color(s_status, th_muted(), 0);
    lv_label_set_text(s_status, "");

    s_kb = lv_keyboard_create(s_root);
    lv_obj_set_size(s_kb, 480, 240);
    lv_obj_set_pos(s_kb, 0, 240);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, on_kb, LV_EVENT_ALL, nullptr);

    return s_root;
}

void screen_config_open(void) {
    if (!s_root) return;
    const DeviceConfig& c = ui_device_config();
    char port[8];
    snprintf(port, sizeof(port), "%u", (unsigned)c.mqtt_port);
    lv_textarea_set_text(s_field[F_SSID],  c.wifi_ssid.c_str());
    lv_textarea_set_text(s_field[F_WPASS], c.wifi_pass.c_str());
    lv_textarea_set_text(s_field[F_HOST],  c.mqtt_host.c_str());
    lv_textarea_set_text(s_field[F_PORT],  port);
    lv_textarea_set_text(s_field[F_USER],  c.mqtt_user.c_str());
    lv_textarea_set_text(s_field[F_MPASS], c.mqtt_pass.c_str());
    lv_textarea_set_text(s_field[F_BASE],  c.mqtt_base.c_str());
    say("", th_muted());
    kb_hide();
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
}

void screen_config_close(void) {
    if (!s_root) return;
    kb_hide();
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}
