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
// 35 s wide at the 1 h zoom and ~100 min at 7 d, so at the long zooms it
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

// Exclusive right edge of the trend window: now, or the newest sample if the
// clock is behind it. Anchoring to now (not the newest sample) makes a sensor
// outage -- which stops history growing -- show as a gap on the right rather
// than freezing the old trace in place.
int64_t chart_window_end(int64_t newest_epoch, int64_t now_epoch);
