// Waveshare ESP32-S3-Touch-LCD-4B ("Smart 86 Box") board config.
// Source of truth: Waveshare's demo package (Arduino-v3.2.0 examples,
// 06_LVGL_Arduino_v9) and the board schematic (ESP32-S3-Touch-LCD-4B.pdf).
// Do NOT tweak pins/timings here without checking those.
//
//   I2C0 (shared bus):  SDA=GPIO47  SCL=GPIO48   (GT911, TCA9554, AXP2101, PCF85063)
//   TCA9554 expander @0x20: P0=LCD CS, P1=LCD MOSI, P2=LCD SCK, P7=LCD RST
//                           P5=TP RST, P6=TP INT (held low at boot -> GT911 addr 0x5D)
//   ST7701 RGB565:  DE=17 VSYNC=3 HSYNC=46 PCLK=9
//                   B0..4 = 10,11,12,13,14
//                   G0..5 = 21,8,18,45,38,39
//                   R0..4 = 40,41,42,2,1
//   RGB timing:     hsync pol=1 FP=10 PW=8 BP=50 ; vsync pol=1 FP=10 PW=8 BP=20
//                   (pclk left at Arduino_GFX's ESP32RGBPanel default, as the demo does)
//   Backlight:      BL net -> GPIO4 (schematic U5 IO4)
//   Panel init:     st7701_type1_init_operations via 3-wire SPI bit-banged
//                   through the TCA9554 (Arduino_XCA9554SWSPI)
#include "display_gfx.h"
#include "platform.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrvGT911.hpp>

static constexpr int kBacklightPin = 4;

// ST7701 init sequence for THIS panel, translated 1:1 from Waveshare's BSP
// (waveshare/esp32_s3_touch_lcd_4b v2.0.0, lcd_init_cmds[]). The generic
// st7701_type1_init_operations differs in the power/pump registers (C2, VGH
// 0x87 vs 0x07, VCOM, missing SLPOUT delay and EB/EC/ED) and the panel blacks
// out a few seconds after init with it.
static const uint8_t ws4b_st7701_init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,                       // SLPOUT
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,                       // page 0x10
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x21, 0x08,
    WRITE_C8_D8, 0xCD, 0x08,
    WRITE_COMMAND_8, 0xB0,                       // positive gamma
    WRITE_BYTES, 16,
    0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18,
    WRITE_COMMAND_8, 0xB1,                       // negative gamma
    WRITE_BYTES, 16,
    0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18,
    WRITE_COMMAND_8, 0xFF,                       // page 0x11
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
    WRITE_C8_D8, 0xB0, 0x60,
    WRITE_C8_D8, 0xB1, 0x30,                     // VCOM
    WRITE_C8_D8, 0xB2, 0x87,                     // VGH
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x49,                     // VGL
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x21,                     // AVDD/AVCL
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,
    END_WRITE,
    DELAY, 20,                                   // pump settle

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x1B, 0x02,
    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11,
    0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00,
    0x00, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12,
    0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00,
    0xEC, 0xA0, 0x00, 0x00,
    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,
    WRITE_C8_D16, 0xE4, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16,
    0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0,
    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,
    WRITE_C8_D16, 0xE7, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16,
    0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0,
    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40,
    WRITE_C8_D16, 0xEC, 0x3C, 0x00,
    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16,
    0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA,
    WRITE_COMMAND_8, 0xFF,                       // back to page 0
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
    WRITE_C8_D8, 0x36, 0x00,                     // MADCTL
    WRITE_C8_D8, 0x3A, 0x66,                     // COLMOD 18-bit
    WRITE_COMMAND_8, 0x21,                       // INVON
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,                       // DISPON
    END_WRITE,
};

static Arduino_XCA9554SWSPI* s_expander = nullptr;
static Arduino_ESP32RGBPanel* s_rgbpanel = nullptr;
static Arduino_RGB_Display* s_gfx = nullptr;
static TouchDrvGT911 s_touch;
static bool s_touch_ok = false;

// PARTIAL render mode: LVGL draws bands into off-screen PSRAM buffers and
// each band is copied into the scanned framebuffer. Rendering directly into
// the framebuffer (DIRECT mode) put intermediate draw states on glass ->
// visible flicker; the memcpy per band is quick enough not to show.
static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    s_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map,
                              lv_area_get_width(area), lv_area_get_height(area));
    lv_display_flush_ready(disp);
}

// The RGB framebuffer wants 2-pixel-aligned update rectangles.
static void rounder_cb(lv_event_t* e) {
    lv_area_t* a = (lv_area_t*)lv_event_get_param(e);
    a->x1 &= ~1; a->y1 &= ~1;
    a->x2 |= 1;  a->y2 |= 1;
}

// Single GT911 poller: the LVGL read cb. display_touched() must not poll the
// controller itself (getPoint consumes the sample; two pollers race), so it
// reads the state cached here.
static volatile bool s_pressed = false;
static volatile int16_t s_last_x = 0, s_last_y = 0;
static volatile uint32_t s_last_touch_ms = 0;

static void touch_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    int16_t x[1], y[1];
    if (s_touch_ok && s_touch.getPoint(x, y, 1) > 0) {
        s_pressed = true; s_last_x = x[0]; s_last_y = y[0];
        s_last_touch_ms = millis();
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x[0];
        data->point.y = y[0];
    } else {
        s_pressed = false;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool display_touched() { return s_pressed; }

uint32_t display_last_touch_ms() { return s_last_touch_ms; }

// Boot-time hold is decided during display_init() (see there); the GT911
// reset dance re-baselines the sensor WITH a resting finger included, so a
// held finger is invisible to any post-init read.
static bool s_boot_touch_held = false;

bool display_touch_held(uint32_t) { return s_boot_touch_held; }

// Raw GT911 point-count read (status reg 0x814E), no SensorLib: talks to the
// still-running controller from the previous session, before this boot's
// reset dance. Returns -1 when the controller doesn't answer, 0 for "no
// fresh sample", else the touch count.
static int gt911_raw_touches() {
    Wire.beginTransmission(0x5D);
    Wire.write(0x81); Wire.write(0x4E);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom(0x5D, 1) != 1) return -1;
    uint8_t st = Wire.read();
    if (st & 0x80) {
        Wire.beginTransmission(0x5D);            // clear the buffer flag
        Wire.write(0x81); Wire.write(0x4E); Wire.write(0);
        Wire.endTransmission();
        return st & 0x0F;
    }
    return 0;
}

// The backlight is ACTIVE-LOW (BSP inverts: duty = 100% - brightness); 255=full on.
void display_set_brightness(uint8_t level) {
    ledcWrite(kBacklightPin, 255 - level);
}

void display_init() {
    // Bus clear: a soft reset (config_request_save/_clear) can interrupt a
    // GT911 read mid-transaction, leaving it holding SDA low — which then
    // corrupts the first I2C transactions of the next boot. Clock SCL until
    // the slave releases SDA, then issue a STOP.
    pinMode(47, INPUT_PULLUP);              // SDA
    pinMode(48, INPUT_PULLUP);              // SCL
    if (digitalRead(47) == LOW) {
        Serial.println("display: I2C SDA stuck low, clearing bus");
        pinMode(48, OUTPUT_OPEN_DRAIN);
        for (int i = 0; i < 9 && digitalRead(47) == LOW; i++) {
            digitalWrite(48, LOW);  delayMicroseconds(5);
            digitalWrite(48, HIGH); delayMicroseconds(5);
        }
        pinMode(47, OUTPUT_OPEN_DRAIN);     // STOP: SDA rises while SCL high
        digitalWrite(47, LOW);  delayMicroseconds(5);
        digitalWrite(47, HIGH); delayMicroseconds(5);
    }
    Wire.begin(47, 48);

    // Boot touch-hold gesture, sampled BEFORE the GT911 reset dance below
    // (resetting re-baselines the sensor with a resting finger included,
    // hiding it). Requires a touch within 400 ms that persists for ~1.2 s.
    {
        uint32_t t0 = millis();
        bool started = false;
        while (millis() - t0 < 400 && !started) {
            if (gt911_raw_touches() > 0) started = true;
            delay(30);
        }
        if (started) {
            s_boot_touch_held = true;
            uint32_t t1 = millis();
            while (millis() - t1 < 1200) {
                if (gt911_raw_touches() == 0) {          // tolerate one empty scan
                    delay(20);
                    if (gt911_raw_touches() == 0) { s_boot_touch_held = false; break; }
                }
                delay(40);
            }
        }
    }

    s_expander = new Arduino_XCA9554SWSPI(7 /* RST */, 0 /* CS */, 2 /* SCK */,
                                          1 /* MOSI */, &Wire, 0x20);
    s_rgbpanel = new Arduino_ESP32RGBPanel(
        17 /* DE */, 3 /* VSYNC */, 46 /* HSYNC */, 9 /* PCLK */,
        10 /* B0 */, 11 /* B1 */, 12 /* B2 */, 13 /* B3 */, 14 /* B4 */,
        21 /* G0 */, 8 /* G1 */, 18 /* G2 */, 45 /* G3 */, 38 /* G4 */, 39 /* G5 */,
        40 /* R0 */, 41 /* R1 */, 42 /* R2 */, 2 /* R3 */, 1 /* R4 */,
        // NO bounce buffer, deliberately: the bounce-refill ISR copies from the
        // PSRAM framebuffer in CPU context, and any flash write (NVS, WiFi
        // persist) disables the cache -> "Cache disabled but cached memory
        // region accessed" panic. The Arduino core lacks the XIP-from-PSRAM
        // config the IDF BSP relies on. Plain GDMA scan-out of the PSRAM
        // framebuffer is hardware-only and survives flash writes.
        1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
        1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);
    s_gfx = new Arduino_RGB_Display(
        480, 480, s_rgbpanel, 0 /* rotation */, true /* auto_flush */,
        s_expander, GFX_NOT_DEFINED /* RST */,
        ws4b_st7701_init_operations, sizeof(ws4b_st7701_init_operations));

    // GT911 reset dance through the expander: hold INT (P6) low while pulsing
    // RST (P5) so the controller latches I2C address 0x5D. Mirrors the demo.
    s_expander->pinMode(5, OUTPUT);
    s_expander->pinMode(6, OUTPUT);
    s_expander->digitalWrite(6, LOW);
    delay(200);
    s_expander->digitalWrite(5, LOW);
    delay(200);
    s_expander->digitalWrite(5, HIGH);
    delay(200);

    Serial.printf("display: gfx begin -> %s\n", s_gfx->begin() ? "ok" : "FAILED");
    s_gfx->fillScreen(RGB565_BLACK);

    ledcAttach(kBacklightPin, 5000 /* Hz */, 8 /* bits */);
    display_set_brightness(255);

    s_touch.setPins(-1, -1);
    s_touch_ok = s_touch.begin(Wire, GT911_SLAVE_ADDRESS_L, 47, 48);
    if (!s_touch_ok) Serial.println("display: GT911 init failed");
    // Note: no setMaxTouchPoint(1) — its config-register rewrite intermittently
    // crashes in i2c on boots that follow an NVS-erase-heavy start (observed
    // deterministic on portal-Reset reboots), and we only ever read one point.

    lv_init();
    lv_tick_set_cb(platform_now_ms);

    // Two off-screen band buffers in PSRAM (1/6 screen each).
    const uint32_t buf_bytes = 480 * 80 * 2;
    lv_color_t* buf1 = (lv_color_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    lv_color_t* buf2 = (lv_color_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);

    lv_display_t* disp = lv_display_create(480, 480);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, nullptr);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    // display_design.md §6: long-press is >= 800 ms (LVGL default is 400).
    lv_indev_set_long_press_time(indev, 800);
}
