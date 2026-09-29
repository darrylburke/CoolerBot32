#include "chart.h"
#include "history.h"

// Which unit held a column, from its tally of samples. Ties go to the lower
// unit number, and a tie against "off" goes to the unit: a column that was
// half spent cooling is more usefully drawn as a run than as idle.
static uint8_t winner(const uint16_t v[3]) {
    if (v[1] && v[1] >= v[0] && v[1] >= v[2]) return 1;
    if (v[2] && v[2] >= v[0]) return 2;
    return 0;
}

void chart_downsample(const History& h, int64_t from, int64_t to,
                      Column* out, size_t ncols) {
    for (size_t i = 0; i < ncols; i++)
        out[i] = Column{0, 0, 0, 0, false, 0};
    if (ncols == 0 || to <= from) return;

    // History hands samples back oldest-first, so all of a column's samples
    // arrive together: tally the running unit for the column in flight and
    // settle it when the column index moves on. Three counters, no buffer.
    size_t cur = (size_t)-1;
    uint16_t votes[3] = {0, 0, 0};

    const int64_t span = to - from;
    for (size_t i = 0; i < h.size(); i++) {
        const Sample& s = h.at(i);
        if (s.t < from || s.t >= to) continue;
        size_t c = (size_t)(((s.t - from) * (int64_t)ncols) / span);
        if (c >= ncols) c = ncols - 1;
        if (c != cur) {
            if (cur < ncols) out[cur].ac = winner(votes);
            cur = c;
            votes[0] = votes[1] = votes[2] = 0;
        }
        const float t = s.temp_c10 / 10.0f;
        const float rh = s.rh_c10 / 10.0f;
        if (!out[c].has) {
            out[c] = Column{t, t, rh, rh, true, 0};
        } else {
            if (t  < out[c].tmin) out[c].tmin = t;
            if (t  > out[c].tmax) out[c].tmax = t;
            if (rh < out[c].hmin) out[c].hmin = rh;
            if (rh > out[c].hmax) out[c].hmax = rh;
        }
        if (s.ac <= 2) votes[s.ac]++;
    }
    if (cur < ncols) out[cur].ac = winner(votes);
}

int64_t chart_window_end(int64_t newest_epoch, int64_t now_epoch) {
    return (now_epoch > newest_epoch ? now_epoch : newest_epoch) + 1;
}
