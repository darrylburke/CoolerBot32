#include "theme.h"
#include <string>

static const lv_font_t* mk(const std::string& path, uint32_t size) {
    return lv_freetype_font_create(path.c_str(),
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, size, LV_FREETYPE_FONT_STYLE_NORMAL);
}

ThemeFonts load_fonts(const char* dir) {
    std::string d = dir;
    ThemeFonts f;
    f.hero     = mk(d + "/Inter-SemiBold.ttf", 30);
    f.title    = mk(d + "/Inter-SemiBold.ttf", 18);
    f.body     = mk(d + "/Inter-Medium.ttf", 13);
    f.overline = mk(d + "/Inter-Medium.ttf", 10);
    f.num_sm   = mk(d + "/JetBrainsMono-Regular.ttf", 11);
    f.num_md   = mk(d + "/JetBrainsMono-Medium.ttf", 13);
    // Boot wordmark only -- display_design.md §5.1 spec's it at 42px bold.
    // (The expanded view's "larger stats hero" reuses the existing `hero`
    // tier: measuring mockups/04_expanded_claude.png vs 01_dashboard.png,
    // the rendered token-value glyphs are ~24px vs ~21px tall -- a modest
    // bump, not a jump to a 42pt tier -- so "larger" there is mostly the
    // much bigger card giving the same font more room, plus a hero-sized
    // header instead of the dashboard's small title.)
    f.hero_lg  = mk(d + "/Inter-SemiBold.ttf", 42);
    return f;
}
