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

    // Node-RED's history is one sample a minute. Where a column is narrower
    // than that (the 1 h zoom), a minute-sampled stretch leaves every other
    // column empty; a single empty column between two filled ones is that,
    // not an outage, so it takes its left neighbour. Wider columns keep
    // every gap: there one empty column is a real outage.
    if (span < (int64_t)ncols * 60)
        for (size_t i = 1; i + 1 < ncols; i++)
            if (!out[i].has && out[i - 1].has && out[i + 1].has) out[i] = out[i - 1];
}

Span chart_joined_temp(const Column* cols, size_t i) {
    const Column& c = cols[i];
    if (i == 0 || !cols[i - 1].has) return Span{c.tmin, c.tmax};
    const Column& p = cols[i - 1];
    return Span{c.tmin < p.tmax ? c.tmin : p.tmax, c.tmax > p.tmin ? c.tmax : p.tmin};
}

Span chart_joined_rh(const Column* cols, size_t i) {
    const float mid = (cols[i].hmin + cols[i].hmax) * 0.5f;
    if (i == 0 || !cols[i - 1].has) return Span{mid, mid};
    const float pm = (cols[i - 1].hmin + cols[i - 1].hmax) * 0.5f;
    return Span{mid < pm ? mid : pm, mid > pm ? mid : pm};
}

// Integral (degC*s) and seconds on record of the temperature over [a, b).
static void integrate(const History& h, int64_t a, int64_t b, double& area, double& cover) {
    area = cover = 0;
    const size_t n = h.size();
    if (n < 2 || b <= a) return;
    size_t lo = 0, hi = n;                      // first sample at or after a
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (h.at(mid).t < a) lo = mid + 1; else hi = mid;
    }
    for (size_t i = lo ? lo - 1 : 0; i + 1 < n; i++) {
        const Sample& p = h.at(i);
        const Sample& q = h.at(i + 1);
        if (p.t >= b) break;
        const int64_t dt = q.t - p.t;
        if (dt <= 0 || dt > kAvgMaxGapS) continue;
        const int64_t x0 = p.t > a ? p.t : a;
        const int64_t x1 = q.t < b ? q.t : b;
        if (x1 <= x0) continue;
        const double v0 = p.temp_c10 + (q.temp_c10 - p.temp_c10) * (double)(x0 - p.t) / dt;
        const double v1 = p.temp_c10 + (q.temp_c10 - p.temp_c10) * (double)(x1 - p.t) / dt;
        area += (v0 + v1) / 2.0 * (double)(x1 - x0) / 10.0;
        cover += (double)(x1 - x0);
    }
}

bool chart_window_mean(const History& h, int64_t from, int64_t to, float& out) {
    double area, cover;
    integrate(h, from, to, area, cover);
    if (cover <= 0) return false;
    out = (float)(area / cover);
    return true;
}

void chart_rolling_avg(const History& h, int64_t from, int64_t to, size_t ncols,
                       float* out, bool* has) {
    for (size_t c = 0; c < ncols; c++) has[c] = false;
    if (ncols == 0 || to <= from || h.size() < 2) return;
    const int64_t start = h.oldest_epoch();
    const int64_t span = to - from;
    for (size_t c = 0; c < ncols; c++) {
        const int64_t te = from + (span * (int64_t)(c + 1)) / (int64_t)ncols;
        const int64_t a = te - kAvgWindowS;
        if (a < start) continue;
        double area, cover;
        integrate(h, a, te, area, cover);
        if (cover < 0.9 * kAvgWindowS) continue;
        out[c] = (float)(area / cover);
        has[c] = true;
    }
}

void series_toggle(SeriesShown& s, Series which) {
    bool& b = which == Series::Temp ? s.temp : which == Series::Rh ? s.rh : s.avg;
    b = !b;
}

int64_t chart_window_end(int64_t newest_epoch, int64_t now_epoch) {
    return (now_epoch > newest_epoch ? now_epoch : newest_epoch) + 1;
}
