#pragma once
#include <stdint.h>
#include <stddef.h>

// 16 bytes with padding. 7 days at 30 s spacing = 20160 samples = 322 KB,
// comfortable in the S3's 8 MB PSRAM. Per-sample epoch (rather than assuming
// even spacing) keeps the time axis correct across MQTT dropouts.
//
// `ac` rides in padding the struct was already carrying (8 + 2 + 2 = 12 bytes
// rounded up to 16), so colouring the trend by unit costs nothing in memory.
struct Sample {
    int64_t t;          // epoch seconds
    int16_t temp_c10;   // degrees C x10
    int16_t rh_c10;     // %RH x10
    uint8_t ac;         // 1 = relay closed (cooling requested) at this sample, else 0.
                        // 2 was AC 2 under the v3 controller; nothing records it now.
};

class History {
public:
    ~History();
    bool init(size_t capacity);   // false on cap==0 or allocation failure
    void clear();

    // Insert the n given samples (sorted by t) among the held ones in time
    // order. Held samples always win: one at a time already held is dropped.
    // If the result is over capacity the oldest go.
    void merge_in(const Sample* s, size_t n);

    // Store only if at least min_gap_s has elapsed since the last stored
    // sample. /data publishes on change as well as every 30 s, so without
    // this gate a burst of relay activity over-samples that period.
    bool maybe_append(int64_t epoch, float temp, float rh, int min_gap_s,
                      uint8_t ac = 0);

    size_t size() const { return count_; }
    size_t capacity() const { return cap_; }
    const Sample& at(size_t i) const;   // 0 = oldest
    int64_t newest_epoch() const;
    int64_t oldest_epoch() const;

private:
    Sample* buf_ = nullptr;
    size_t cap_ = 0;
    size_t head_ = 0;    // index of oldest
    size_t count_ = 0;
};
