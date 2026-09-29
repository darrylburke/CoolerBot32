#pragma once
#include "lvgl.h"
struct ThemeFonts {
    const lv_font_t *hero=nullptr, *title=nullptr, *body=nullptr,
                    *overline=nullptr, *num_sm=nullptr, *num_md=nullptr,
                    *hero_lg=nullptr;   // boot wordmark only (42px, display_design.md §5.1)
};
void theme_set_fonts(const ThemeFonts& f);
const ThemeFonts& theme_fonts();
lv_color_t th_page(); lv_color_t th_surface(); lv_color_t th_ink(); lv_color_t th_ink2();
lv_color_t th_muted(); lv_color_t th_grid();
lv_color_t th_claude(); lv_color_t th_codex();
lv_color_t th_good(); lv_color_t th_warn(); lv_color_t th_crit();
// Card fill for a unit that owns the current window but is resting mid-duty.
lv_color_t th_duty();
// Quiet Slate accent set: only the live thing is coloured. th_cool* doubles
// as AC 1's identity, th_ac2* is AC 2's -- the trend bars and the unit cards
// share them, so the cards read as the graph's legend.
lv_color_t th_cool(); lv_color_t th_cool_bg(); lv_color_t th_cool_dim(); lv_color_t th_rh();
lv_color_t th_ac2();  lv_color_t th_ac2_bg();  lv_color_t th_ac2_dim();
// Vendor-status severities that the good/warn/crit trio doesn't cover.
lv_color_t th_sev_major(); lv_color_t th_sev_maint();
