// Sim-only: stb_image is NOT allowed under shared/ (portability_guard.sh),
// so PNG decoding lives here and hands shared/ui/screen_boot.cpp a plain
// lv_image_dsc_t it can hand straight to lv_image_set_src().
#include "lvgl.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <cstdio>

const lv_image_dsc_t* logo_load(const char* path) {
    int w, h, n;
    unsigned char* px = stbi_load(path, &w, &h, &n, 4);   // force RGBA8888
    if (!px) {
        fprintf(stderr, "logo_load: failed to open %s\n", path);
        return nullptr;
    }
    // stb gives straight RGBA; LVGL's ARGB8888 is byte order {B,G,R,A} on a
    // little-endian system (i.e. 0xAARRGGBB packed as bytes B,G,R,A) --
    // screenshot.cpp's RGB888 write-path does the same R/B swap for the
    // opposite direction (LVGL -> PNG). Swap here (PNG -> LVGL).
    for (int i = 0; i < w * h; i++) {
        unsigned char t = px[i * 4];
        px[i * 4] = px[i * 4 + 2];
        px[i * 4 + 2] = t;
    }
    // Heap-allocated (and intentionally never freed): lv_image_dsc_t's
    // `data` pointer must stay valid for as long as any lv_image widget
    // references it, and the boot screen now loads two logos, so a static
    // single descriptor would alias them.
    lv_image_dsc_t* dsc = new lv_image_dsc_t();
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;  // required: lv_image_src_get_type()
                                                 // reads byte 0 of *src and only
                                                 // treats it as LV_IMAGE_SRC_VARIABLE
                                                 // when it's < 0x20 (see lv_draw_image.c).
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.flags = 0;
    dsc->header.w = (uint32_t)w;
    dsc->header.h = (uint32_t)h;
    dsc->header.stride = (uint32_t)w * 4;
    dsc->data_size = (uint32_t)(w * h * 4);
    dsc->data = px;
    return dsc;
}
