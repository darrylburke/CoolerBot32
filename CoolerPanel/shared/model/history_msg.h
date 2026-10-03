#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "history.h"

// <base>/history from Node-RED: the last 24 h in one-minute slots. samples
// cover [from, to); slots without temperature or humidity are left out.
struct HistoryMsg {
    int64_t from = 0;
    int64_t to = 0;
    std::vector<Sample> samples;
};

// False for anything that is not a sound v1 window. now is the panel's clock;
// the "not from the future" check is skipped while it is unset.
bool history_msg_parse(const char* json, size_t len, int64_t now, HistoryMsg& out);

// True when m has a sample in a minute of [m.from, m.to) where h has none.
// /history arrives with every /data, so a panel that has been connected all
// along skips the merge rather than rebuilding its ring twice a minute.
bool history_adds_coverage(const History& h, const HistoryMsg& m);
