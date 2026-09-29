#include "theme.h"
static ThemeFonts g_fonts;
void theme_set_fonts(const ThemeFonts& f) { g_fonts = f; }
const ThemeFonts& theme_fonts() { return g_fonts; }
lv_color_t th_page()   { return lv_color_hex(0x171a1d); }
lv_color_t th_surface(){ return lv_color_hex(0x1e2226); }
lv_color_t th_ink()    { return lv_color_hex(0xeef1f3); }
lv_color_t th_ink2()   { return lv_color_hex(0x8d959c); }
lv_color_t th_muted()  { return lv_color_hex(0x59616a); }
lv_color_t th_grid()   { return lv_color_hex(0x2a2f34); }
// Mid-grey, deliberately well clear of th_page()'s near-black so the
// Duty and Off card states are told apart at a glance, not by squinting.
lv_color_t th_duty()   { return lv_color_hex(0x2c333a); }
lv_color_t th_claude() { return lv_color_hex(0xcf6a44); }
lv_color_t th_codex()  { return lv_color_hex(0x3987e5); }
lv_color_t th_good()   { return lv_color_hex(0x0ca30c); }
lv_color_t th_warn()   { return lv_color_hex(0xfab219); }
lv_color_t th_crit()   { return lv_color_hex(0xd03b3b); }
// Orange for a major outage: distinct from th_warn()'s amber (minor) and from
// the Claude accent 0xcf6a44, which would read as the brand glyph.
lv_color_t th_sev_major(){ return lv_color_hex(0xf2721a); }
// Cyan-blue for scheduled maintenance: deliberately not the Codex accent
// 0x3987e5, which would collide with that half's own glyph.
lv_color_t th_sev_maint(){ return lv_color_hex(0x53a8c4); }

// Quiet Slate's working accent. Only the thing that is actually happening
// right now gets colour; everything else stays in the greys.
lv_color_t th_cool()    { return lv_color_hex(0x63b6d4); }   // live / cooling / AC 1
lv_color_t th_cool_bg() { return lv_color_hex(0x1b2b33); }   // its card fill
lv_color_t th_cool_dim(){ return lv_color_hex(0x4d7d90); }   // its label
lv_color_t th_rh()      { return lv_color_hex(0x7d8f6a); }   // humidity trace

// AC 2's identity colour. The trend colours each bar by the unit that was
// running, so the two need telling apart -- and AC 2's card takes the same
// colour when it runs, which makes the card the graph's legend and saves
// spending screen space on a real one.
//
// Periwinkle rather than another blue-green: it has to hold up against
// th_cool() AND against th_rh()'s olive humidity trace on the same 408px
// canvas, and nothing else on the screen occupies the violet end.
lv_color_t th_ac2()     { return lv_color_hex(0x9d92d8); }
lv_color_t th_ac2_bg()  { return lv_color_hex(0x272438); }
lv_color_t th_ac2_dim() { return lv_color_hex(0x6f68a0); }
