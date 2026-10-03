#pragma once
#include <stddef.h>
#include <stdint.h>
struct CoolerState;

// Topic tree root the router matches against. Default "cooler"; set from
// the panel config at startup.
void router_set_prefix(const char* prefix);
const char* router_prefix(void);

// True if topic is exactly "<prefix>/<leaf>".
bool router_is_leaf(const char* topic, const char* leaf);

// Returns true if the message was recognised and applied.
bool route_message(CoolerState& s, const char* topic, const char* payload,
                   size_t len, int64_t now_epoch);
