#include <doctest/doctest.h>
#include "commands.h"
#include "cooler_state.h"
#include "mqtt_router.h"
#include "test_publish_stub.h"
#include <cstring>
#include <cstdio>

static CoolerState base_state() {
    CoolerState s;
    s.valid = true;
    s.coolerset = 4; s.range = 2; s.maxrun = 15;
    s.minofftime = 5; s.dutypercent = 80; s.minruntime = 180;
    s.fin_cutoff = 0; s.fin_recover = 3; s.settle = 10; s.sampleinterval = 3600;
    return s;
}

TEST_CASE("request debounces then publishes once") {
    test_publish_reset(false);
    router_set_prefix("ha/esp32-cooler");
    Commands c;
    CoolerState s = base_state();

    c.request("coolerset", 5, 1000);
    c.tick(1100);                              // still inside the debounce
    CHECK(test_publishes().empty());
    c.tick(1401);                              // 401 ms later -> flush
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].topic == "ha/esp32-cooler/cmd");
    CHECK(test_publishes()[0].payload == "{\"coolerset\":5}");
    CHECK(test_publishes()[0].retain == false);
}

TEST_CASE("rapid repeats coalesce into a single publish of the last value") {
    test_publish_reset(false);
    Commands c;
    c.request("coolerset", 5, 1000);
    c.request("coolerset", 6, 1100);
    c.request("coolerset", 7, 1200);
    c.tick(1250);
    CHECK(test_publishes().empty());
    c.tick(1601);
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].payload == "{\"coolerset\":7}");
}

TEST_CASE("requests are clamped before they are sent") {
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);                    // learn minofftime = 5
    c.request("coolerset", 99, 1000);
    c.tick(1500);
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].payload == "{\"coolerset\":40}");
}

TEST_CASE("fin_recover clamp uses the live fin_cutoff") {
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    s.fin_cutoff = 4;
    c.on_state(s);
    c.request("fin_recover", 3, 1000);
    c.tick(1500);
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].payload == "{\"fin_recover\":5}");
}

TEST_CASE("negative settings are published and reconciled") {
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);
    c.request("fin_cutoff", -2, 1000);
    c.tick(1500);
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].payload == "{\"fin_cutoff\":-2}");
    s.fin_cutoff = -2;
    c.on_state(s);
    CHECK_FALSE(c.is_pending("fin_cutoff"));
    CHECK(c.display_value("fin_cutoff", s) == -2);
}

TEST_CASE("action publishes immediately without pending") {
    test_publish_reset(false);
    router_set_prefix("ha/esp32-cooler");
    Commands c;
    CHECK(c.action("calibrate", 1));
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].topic == "ha/esp32-cooler/cmd");
    CHECK(test_publishes()[0].payload == "{\"calibrate\":1}");
    CHECK_FALSE(c.is_pending("calibrate"));
    char toast[64];
    CHECK_FALSE(c.take_toast(toast, sizeof(toast)));
}

TEST_CASE("a failed action raises a toast") {
    test_publish_reset(true);
    Commands c;
    CHECK_FALSE(c.action("fincal_reset", 1));
    char toast[64];
    REQUIRE(c.take_toast(toast, sizeof(toast)));
    CHECK(std::strstr(toast, "fincal_reset") != nullptr);
}

TEST_CASE("pending shows the requested value until the cooler confirms") {
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);
    CHECK(c.display_value("coolerset", s) == 4);

    c.request("coolerset", 6, 1000);
    c.tick(1500);
    CHECK(c.is_pending("coolerset"));
    CHECK(c.display_value("coolerset", s) == 6);   // optimistic

    s.coolerset = 6;
    c.on_state(s);
    CHECK_FALSE(c.is_pending("coolerset"));
    CHECK(c.display_value("coolerset", s) == 6);
}

TEST_CASE("a clamped-by-cooler answer clears pending and shows the truth") {
    // Panel asks for 40 (legal), the cooler comes back with something else.
    // The panel must show what the cooler says, not what it asked for.
    // 30 differs from the pre-publish 4, so it counts as our answer.
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);
    c.request("coolerset", 40, 1000);
    c.tick(1500);
    CHECK(c.display_value("coolerset", s) == 40);

    s.coolerset = 30;                 // cooler decided otherwise
    c.on_state(s);
    CHECK_FALSE(c.is_pending("coolerset"));
    CHECK(c.display_value("coolerset", s) == 30);
}

TEST_CASE("an unrelated periodic /data does not clear pending") {
    // The cooler publishes every 30 s regardless. If that cleared pending,
    // a dropped command would read as success and never time out.
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);
    c.request("coolerset", 6, 1000);
    c.tick(1500);
    REQUIRE(c.is_pending("coolerset"));

    c.on_state(s);                    // periodic publish, coolerset still 4
    CHECK(c.is_pending("coolerset"));  // still waiting
    CHECK(c.display_value("coolerset", s) == 6);

    c.tick(1500 + CMD_DEADLINE_MS + 1);
    CHECK_FALSE(c.is_pending("coolerset"));
    char toast[64];
    CHECK(c.take_toast(toast, sizeof(toast)));
}

TEST_CASE("setting a value to what it already is still clears") {
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);
    c.request("coolerset", 4, 1000);   // already 4
    c.tick(1500);
    c.on_state(s);                     // reported == want
    CHECK_FALSE(c.is_pending("coolerset"));
}

TEST_CASE("no acknowledgement within the deadline reverts and raises a toast") {
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);
    c.request("coolerset", 6, 1000);
    c.tick(1500);                     // published, pending
    REQUIRE(c.is_pending("coolerset"));

    c.tick(1500 + CMD_DEADLINE_MS + 1);
    CHECK_FALSE(c.is_pending("coolerset"));
    CHECK(c.display_value("coolerset", s) == 4);   // reverted

    char toast[64];
    REQUIRE(c.take_toast(toast, sizeof(toast)));
    CHECK(std::strstr(toast, "coolerset") != nullptr);
    CHECK_FALSE(c.take_toast(toast, sizeof(toast)));  // one-shot
}

TEST_CASE("a failed publish reverts immediately") {
    test_publish_reset(true);         // publish returns false
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);
    c.request("coolerset", 6, 1000);
    c.tick(1500);
    CHECK_FALSE(c.is_pending("coolerset"));
    char toast[64];
    CHECK(c.take_toast(toast, sizeof(toast)));
}

TEST_CASE("independent keys pend independently") {
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    c.on_state(s);
    c.request("coolerset", 6, 1000);
    c.request("dutypercent", 60, 1000);
    c.tick(1500);
    CHECK(test_publishes().size() == 2);
    CHECK(c.is_pending("coolerset"));
    CHECK(c.is_pending("dutypercent"));

    s.coolerset = 6;
    c.on_state(s);
    CHECK_FALSE(c.is_pending("coolerset"));
    CHECK(c.is_pending("dutypercent"));   // still waiting
}

TEST_CASE("re-request of the same key while the first is in flight still waits for its own ack") {
    // Regression: pre must describe the state just before THIS publish, not
    // whatever it was when the entry was first touched. Sequence, coolerset
    // starting at 4:
    //   1. request(6) -> flush -> sent, awaiting ack.
    //   2. request(4) again before any ack for #1 arrives.
    //   3. /data reports 6 (command #1 landed) -- must NOT be read as an
    //      answer to the still-unsent command #2.
    //   4. flush #2 -> sent, awaiting ack.
    //   5. another /data still reports 6 (an ordinary heartbeat, or the
    //      cooler simply hasn't acted on #2 yet) -- must NOT be mistaken
    //      for an ack of #2 just because it differs from the stale pre=4
    //      a request-time capture would have left behind.
    // Only the deadline may resolve it if no real ack ever arrives.
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();   // coolerset = 4
    c.on_state(s);

    c.request("coolerset", 6, 1000);
    c.tick(1450);                          // flush #1: publishes {"coolerset":6}
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].payload == "{\"coolerset\":6}");
    REQUIRE(c.is_pending("coolerset"));

    c.request("coolerset", 4, 1600);       // re-request before any ack for #1
    CHECK(c.is_pending("coolerset"));

    s.coolerset = 6;                       // /data: cooler landed command #1
    c.on_state(s);
    CHECK(c.is_pending("coolerset"));      // #2 not sent/acked yet -- still waiting

    c.tick(2050);                          // flush #2: publishes {"coolerset":4}
    REQUIRE(test_publishes().size() == 2);
    CHECK(test_publishes()[1].payload == "{\"coolerset\":4}");
    REQUIRE(c.is_pending("coolerset"));

    c.on_state(s);                         // still reports 6 -- not an ack of #2
    CHECK(c.is_pending("coolerset"));

    c.tick(2050 + CMD_DEADLINE_MS + 1);    // no real ack ever arrives
    CHECK_FALSE(c.is_pending("coolerset"));
    char toast[64];
    REQUIRE(c.take_toast(toast, sizeof(toast)));
    CHECK(std::strstr(toast, "coolerset") != nullptr);
}

TEST_CASE("queuing more distinct keys than the pending table holds still surfaces a toast") {
    // BOUNDS[] has 10 settable keys; the pending table must have headroom
    // for all of them. If it's ever exhausted
    // anyway, the dropped edit must not vanish silently -- that's a stealth
    // version of the exact failure this task guards against.
    test_publish_reset(false);
    Commands c;
    char toast[64];
    CHECK_FALSE(c.take_toast(toast, sizeof(toast)));   // clean slate

    char key[8];
    for (size_t i = 0; i < CMD_MAX_PENDING; i++) {
        std::snprintf(key, sizeof(key), "k%zu", i);
        c.request(key, 1, 1000);
    }
    CHECK_FALSE(c.take_toast(toast, sizeof(toast)));   // table exactly full, nothing dropped yet

    c.request("k_overflow", 1, 1000);      // one more distinct key than the table holds
    REQUIRE(c.take_toast(toast, sizeof(toast)));
    CHECK(std::strstr(toast, "k_overflow") != nullptr);
}
