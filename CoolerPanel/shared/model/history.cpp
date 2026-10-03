#include "history.h"
#include <cstdlib>
#include <cmath>
#include <vector>

static int16_t to_c10(float v) {
    float scaled = v * 10.0f;
    if (scaled > 32767.0f) scaled = 32767.0f;
    if (scaled < -32768.0f) scaled = -32768.0f;
    return (int16_t)lroundf(scaled);
}

History::~History() { std::free(buf_); }

bool History::init(size_t capacity) {
    if (capacity == 0) return false;
    std::free(buf_);
    buf_ = (Sample*)std::calloc(capacity, sizeof(Sample));
    if (!buf_) { cap_ = count_ = head_ = 0; return false; }
    cap_ = capacity; count_ = 0; head_ = 0;
    return true;
}

void History::clear() { count_ = 0; head_ = 0; }

bool History::maybe_append(int64_t epoch, float temp, float rh, int min_gap_s,
                           uint8_t ac) {
    if (!buf_) return false;
    if (count_ > 0 && (epoch - newest_epoch()) < (int64_t)min_gap_s) return false;
    // Anything the cooler has not told us about is "no unit running" rather
    // than a third colour on the trend: a bad value would otherwise paint a
    // stripe the legend cannot explain.
    if (ac > 2) ac = 0;
    Sample s{epoch, to_c10(temp), to_c10(rh), ac};
    if (count_ < cap_) {
        buf_[(head_ + count_) % cap_] = s;
        count_++;
    } else {
        buf_[head_] = s;              // overwrite oldest
        head_ = (head_ + 1) % cap_;   // and advance it
    }
    return true;
}

const Sample& History::at(size_t i) const { return buf_[(head_ + i) % cap_]; }
int64_t History::newest_epoch() const { return count_ ? at(count_ - 1).t : 0; }
int64_t History::oldest_epoch() const { return count_ ? at(0).t : 0; }

void History::merge_in(const Sample* s, size_t n) {
    if (!buf_ || n == 0) return;
    std::vector<Sample> out;
    out.reserve(count_ + n);
    size_t i = 0, k = 0;
    while (i < count_ || k < n) {
        if (k == n || (i < count_ && at(i).t <= s[k].t)) {
            if (k < n && at(i).t == s[k].t) k++;   // held sample wins
            out.push_back(at(i++));
        } else {
            out.push_back(s[k++]);
        }
    }
    size_t start = out.size() > cap_ ? out.size() - cap_ : 0;
    count_ = out.size() - start;
    head_ = 0;
    for (size_t j = 0; j < count_; j++) buf_[j] = out[start + j];
}
