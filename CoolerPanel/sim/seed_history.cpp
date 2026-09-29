// Simulator-only: fill the trend with a plausible week so the graph can be
// looked at without waiting for one to happen.
//
// The panel builds its history from live /data, which means a fresh run shows
// an empty canvas. This synthesises the shape the v4 controller produces: the
// single relay cycling on a maxrun-long frame at the given duty, temperature
// sawtoothing against the setpoint band underneath.
//
// Debug affordance, never compiled into the device firmware -- and it seeds
// the panel's own buffer only, so nothing is published anywhere.
#include "app.h"
#include "history.h"
#include "platform.h"
#include <cmath>

void seed_demo_history(int coolerset, int maxrun_min, int dutypercent) {
    History& h = panel_history();
    h.clear();

    const int step = 30;                             // /data's sampling period
    const int64_t now = platform_epoch_utc();
    const int64_t span = 7 * 24 * 3600;
    const int frame = maxrun_min * 60;               // one cooling cycle
    const int on_s = frame * dutypercent / 100;      // relay closed this long

    // A box being held steady neither warms nor cools on average, so the two
    // rates have to balance at THIS duty or the trace walks off the axis.
    const float cool = 0.055f;
    const float warm = cool * (float)on_s / (float)(frame - on_s);

    float t = (float)coolerset + 0.6f;
    for (int64_t age = span; age > 0; age -= step) {
        const int64_t at = now - age;
        const int phase = (int)(at % frame);
        const uint8_t ac = (phase < on_s) ? 1 : 0;   // relay closed

        t += ac ? -cool : warm;
        // Slow diurnal wander on top, so the auto-ranged axis has something
        // to do and the week view isn't a flat band.
        const float day = sinf((float)(at % 86400) * 6.2832f / 86400.0f);
        const float temp = t + day * 0.5f;
        const float rh = 80.0f + day * 4.0f;

        h.maybe_append(at, temp, rh, 0, ac);
    }
}
