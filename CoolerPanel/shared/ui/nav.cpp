#include "nav.h"
#include "screen_trend.h"
#include "screen_settings.h"
#include "screen_detail.h"
#include "screen_alarm.h"
#include "screen_config.h"

// Task 13: three horizontally-swipeable pages (Trend | Settings | Detail) in
// an lv_tileview, with the alarm takeover parented to `root` directly rather
// than into any tile -- it must cover whichever page the operator is
// currently looking at, not be just one more page they could swipe past.
// screen_alarm_create() is called last so it starts above the tileview in
// z-order, and screen_alarm_refresh() additionally calls
// lv_obj_move_foreground() itself on every activation, so this ordering is
// belt-and-braces rather than load-bearing.
//
// Tile scroll directions: the leftmost tile (Trend) only allows swiping
// further right (into Settings); the rightmost tile (Detail) only allows
// swiping further left (into Settings); the middle tile (Settings) allows
// both, so it's reachable from -- and can leave to -- either neighbour.
static lv_obj_t* s_tv = nullptr;

// Idempotency guard (flagged in Task 13's review): a second call used to
// overwrite s_tv, leaking the previous tileview -- and the screen_*_create()
// calls below each have their own single-instance static state that a
// double-build would clobber too. Task 17's boot -> setup -> main flow is
// designed to call this at most once per process (ui_init() reaches it on
// at most one of its return paths), but nothing enforces that at the call
// site, so guard here as well rather than relying solely on that discipline.
void nav_init(lv_obj_t* root) {
    if (s_tv) return;
    s_tv = lv_tileview_create(root);
    lv_obj_set_size(s_tv, 480, 480);

    lv_obj_t* t0 = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t* t1 = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);   // LV_DIR_LEFT | LV_DIR_RIGHT
    lv_obj_t* t2 = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_LEFT);

    screen_trend_create(t0);
    screen_settings_create(t1);
    screen_detail_create(t2);
    screen_config_create(root);   // hidden overlay, opened from Detail
    screen_alarm_create(root);   // sits above the tileview, hidden until active
}

void nav_refresh(void) {
    screen_trend_refresh();
    screen_settings_refresh();
    screen_detail_refresh();
    screen_alarm_refresh();      // shows/hides itself based on Alarms::active()
}

void nav_show_page(int index) {
    if (!s_tv || index < 0 || index > 2) return;
    lv_tileview_set_tile_by_index(s_tv, (uint32_t)index, 0, LV_ANIM_OFF);
}
