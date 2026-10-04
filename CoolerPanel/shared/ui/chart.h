#pragma once
#include <stdint.h>
#include <stddef.h>
class History;

// One horizontal pixel column of the trend. min/max preserve spikes that a
// plain average would erase. `has` is false when no sample fell in the
// column -- render a gap, never interpolate across a data outage.
//
// `ac` is 1 when the relay was closed for most of the column (see
// History's Sample::ac), which is what the bar gets coloured by. A column is
// 35 s wide at the 1 h zoom and ~7 min at 12 h, so at the long zooms it
// reports whether the column was mostly cooling rather than a single run.
struct Column {
    float tmin, tmax;
    float hmin, hmax;
    bool has;
    uint8_t ac;      // 0 none, 1, 2
};

// Bucket history samples in [from, to) into ncols evenly spaced columns.
void chart_downsample(const History& h, int64_t from, int64_t to,
                      Column* out, size_t ncols);

// Vertical extent to draw column i so the trace is continuous. On its own a
// column is just its min..max, and on a steep rise or fall neighbours land at
// different heights without touching, so the curve breaks into dashes.
// Temperature: the column's range widened to meet the previous column's.
// Humidity: from the previous column's midpoint to this one's. A column after
// an empty one (a real gap) is not joined. Only for columns with data.
struct Span { float lo, hi; };
Span chart_joined_temp(const Column* cols, size_t i);
Span chart_joined_rh(const Column* cols, size_t i);

// Which traces the trend draws; both by default. series_toggle() flips one
// and returns true, but refuses (returns false) to hide the last one shown,
// so the graph is never blank.
//
// The rolling average (Series::Avg) is an overlay, not a trace: it toggles
// freely and does not count towards "never neither".
enum class Series { Temp, Rh, Avg };
struct SeriesShown { bool temp = true; bool rh = true; bool avg = true; };
bool series_toggle(SeriesShown& s, Series which);

// Temperature averages, time-weighted the way the trace is drawn: straight
// lines between samples, with any two samples more than kAvgMaxGapS apart
// counting as a gap (nothing in between). Live samples every 30 s therefore
// weigh no more than history every 60 s.
constexpr int64_t kAvgWindowS = 3600;   // about one cooling cycle of this box
constexpr int64_t kAvgMaxGapS = 150;

// Mean over [from, to); false when nothing there is on record.
bool chart_window_mean(const History& h, int64_t from, int64_t to, float& out);

// Rolling kAvgWindowS mean ending at each column's right edge. A column gets
// one (has[c]) only when the record reaches back to the start of its window
// and covers at least 90% of it -- a partial hour would average part of a
// cooling cycle and read high or low.
void chart_rolling_avg(const History& h, int64_t from, int64_t to, size_t ncols,
                       float* out, bool* has);

// Exclusive right edge of the trend window: now, or the newest sample if the
// clock is behind it. Anchoring to now (not the newest sample) makes a sensor
// outage -- which stops history growing -- show as a gap on the right rather
// than freezing the old trace in place.
int64_t chart_window_end(int64_t newest_epoch, int64_t now_epoch);
