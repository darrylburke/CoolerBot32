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

// The samples of m the panel lacks, oldest first, into out (cleared first).
// /history arrives with every /data, so a panel that has been connected all
// along must find nothing here rather than rebuild its ring twice a minute:
//  - a slot counts as held if the panel has a sample within a minute either
//    side of it (its own 30 s gate and clock skew against Node-RED's);
//  - slots newer than a minute before the panel's newest sample belong to
//    live data and are never taken;
//  - samples stamped before the panel's clock was set count for nothing.
void history_missing(const History& h, const HistoryMsg& m, std::vector<Sample>& out);
