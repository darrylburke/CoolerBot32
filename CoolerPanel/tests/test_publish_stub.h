#pragma once
#include <string>
#include <vector>

struct PubRecord { std::string topic, payload; bool retain; };

// Everything published since the last reset, in order.
std::vector<PubRecord>& test_publishes();
// Clear the log. fail=true makes subsequent publishes return false.
void test_publish_reset(bool fail);
