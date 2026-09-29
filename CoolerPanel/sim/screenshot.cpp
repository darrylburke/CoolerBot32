#include "screenshot.h"
#include "lvgl.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

bool screenshot_save(const char* path) {
    lv_draw_buf_t* snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if (!snap) return false;
    int w = snap->header.w, h = snap->header.h;
    uint32_t stride = snap->header.stride;
    uint8_t* data = (uint8_t*)snap->data;
    // LVGL's lv_color_t (and therefore its RGB888 pixel layout) stores bytes
    // as {blue, green, red} -- see lv_color.h -- not {red, green, blue}.
    // stb_image_write expects R,G,B per pixel, so swap here before writing
    // (LVGL's own others/test/lv_test_screenshot_compare.c does the same
    // swap for its RGB888 case). Without this, every non-grayscale color
    // (e.g. the accent/gauge colors) comes out with red and blue swapped.
    for (int y = 0; y < h; y++) {
        uint8_t* row = data + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            uint8_t* px = row + (size_t)x * 3;
            uint8_t tmp = px[0];
            px[0] = px[2];
            px[2] = tmp;
        }
    }
    int ok = stbi_write_png(path, w, h, 3, data, stride);
    lv_draw_buf_destroy(snap);
    return ok != 0;
}
