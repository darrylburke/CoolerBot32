#include <doctest/doctest.h>
#include "dehum_state.h"
#include "dehum_bounds.h"
#include "dehum_status.h"
#include "dehum_commands.h"
#include "test_publish_stub.h"
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// The dehumidifier model (spec 2026-10-31 §1 wire contract, §3 panel), checked
// against the app's DehumStateParser / DehumBounds / DehumStatusText /
// DehumHealth / DehumController behaviour.

static std::string load(const char* name) {
    std::ifstream f(std::string(FIXture_DIR) + "/" + name);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

static DehumState parsed(const char* fixture, int64_t now = 1700000000) {
    DehumState s;
    const std::string j = load(fixture);
    REQUIRE(dehum_parse_data(s, j.data(), j.size(), now));
    return s;
}

static bool parse(DehumState& s, const char* json) {
    return dehum_parse_data(s, json, std::strlen(json), 1700000000);
}

static int setting(const DehumState& s, const char* key) {
    const int i = dehum_key_index(key);
    REQUIRE(i >= 0);
    REQUIRE(s.settings.known[i]);
    return s.settings.v[i];
}

// ------------------------------------------------------------------ parser
TEST_CASE("dehum/data: the live payload parses field by field") {
    const DehumState s = parsed("dehum_data_auto.json");
    CHECK(s.valid);
    CHECK(s.last_rx_epoch == 1700000000);
    CHECK(s.mode == DehumMode::Auto);
    CHECK(s.relay);
    CHECK_FALSE(s.relay2);
    CHECK_FALSE(s.link_lost);
    CHECK(s.hold_s == 593);
    CHECK(s.hold2_s == 0);
    CHECK(s.remain_s == 0);
    CHECK(s.phase == -1);                       // null
    CHECK(s.temp_valid);
    CHECK(s.temp == doctest::Approx(4.30));
    CHECK(s.humidity_valid);
    CHECK(s.humidity == doctest::Approx(98.9));
    CHECK(setting(s, "min_run") == 10);
    CHECK(setting(s, "timer_min") == 120);
    CHECK(setting(s, "cycle_on") == 30);
    CHECK(setting(s, "cycle_off") == 30);
    CHECK(setting(s, "r1_en") == 1);
    CHECK(setting(s, "r1_sensor") == DEHUM_SENSOR_HUM);
    CHECK(setting(s, "r1_cmp") == 1);
    CHECK(setting(s, "r1_value") == 65);
    CHECK(setting(s, "r3_sensor") == DEHUM_SENSOR_TEMP);
    CHECK(setting(s, "r3_relay") == 2);
    CHECK(setting(s, "r4_value") == 2);
    CHECK(s.rule_match[0]);
    CHECK_FALSE(s.rule_match[1]);
    CHECK_FALSE(s.rule_match[2]);
    CHECK_FALSE(s.rule_match[3]);
    CHECK(s.run_s == 7);
    CHECK(s.uptime_s == 0);
    for (int i = 0; i < DEHUM_NKEYS; i++) CHECK(s.settings.known[i]);
}

TEST_CASE("dehum/data: anything but a v1 object is refused and leaves the state alone") {
    DehumState s = parsed("dehum_data_auto.json");
    CHECK_FALSE(parse(s, "{\"v\":2,\"mode\":\"on\"}"));
    CHECK_FALSE(parse(s, "{\"mode\":\"on\"}"));
    CHECK_FALSE(parse(s, "[1,2]"));
    CHECK_FALSE(parse(s, "not json"));
    CHECK_FALSE(parse(s, "{\"v\":\"1\"}"));
    CHECK(s.mode == DehumMode::Auto);
    CHECK(s.temp == doctest::Approx(4.30));
}

TEST_CASE("dehum/data: null readings are no reading; phase and cycle words") {
    const DehumState s = parsed("dehum_data_cycle_linklost.json");
    CHECK(s.mode == DehumMode::Cycle);
    CHECK(s.phase == 0);
    CHECK(s.link_lost);
    CHECK_FALSE(s.temp_valid);
    CHECK_FALSE(s.humidity_valid);
}

TEST_CASE("dehum/data: each message is whole -- absent settings become unknown") {
    DehumState s = parsed("dehum_data_auto.json");
    REQUIRE(parse(s, "{\"v\":1,\"mode\":\"sideways\",\"timer_min\":90,\"min_run\":\"5\",\"cycle_on\":2.5}"));
    CHECK(s.mode == DehumMode::Unknown);
    CHECK(setting(s, "timer_min") == 90);
    CHECK_FALSE(s.settings.known[DK_MIN_RUN]);    // a string is not an integer
    CHECK_FALSE(s.settings.known[DK_CYCLE_ON]);   // nor is a fraction
    CHECK_FALSE(s.settings.known[dehum_key_index("r1_en")]);
    CHECK_FALSE(s.temp_valid);
    CHECK_FALSE(s.relay);
}

TEST_CASE("dehum/availability keeps through data, and only online/offline count") {
    DehumState s;
    CHECK(dehum_parse_availability(s, "offline", 7));
    CHECK(s.availability_seen);
    CHECK_FALSE(s.online);
    CHECK(dehum_parse_availability(s, "online\n", 7));
    CHECK(s.online);
    CHECK_FALSE(dehum_parse_availability(s, "maybe", 5));
    CHECK(s.online);
    const std::string j = load("dehum_data_auto.json");
    REQUIRE(dehum_parse_data(s, j.data(), j.size(), 1));
    CHECK(s.online);
    CHECK(s.availability_seen);
}

TEST_CASE("mode names: wire, labels and the button order") {
    CHECK(dehum_mode_of("timer") == DehumMode::Timer);
    CHECK(dehum_mode_of("AUTO") == DehumMode::Unknown);
    CHECK(std::string(dehum_mode_label(DehumMode::Auto)) == "AUTO (rules)");
    CHECK(std::string(dehum_mode_wire(DehumMode::Cycle)) == "cycle");
    CHECK(DEHUM_MODE_BUTTONS[0] == DehumMode::On);
    CHECK(DEHUM_MODE_BUTTONS[1] == DehumMode::Off);
    CHECK(DEHUM_MODE_BUTTONS[2] == DehumMode::Timer);
    CHECK(DEHUM_MODE_BUTTONS[3] == DehumMode::Auto);
    CHECK(DEHUM_MODE_BUTTONS[4] == DehumMode::Cycle);
}

// ------------------------------------------------------------------ bounds
TEST_CASE("keys: every setting the firmware takes, and nothing else") {
    CHECK(DEHUM_NKEYS == 28);
    CHECK(dehum_key_index("timer_min") == DK_TIMER_MIN);
    CHECK(dehum_key_index("min_run") == DK_MIN_RUN);
    CHECK(dehum_key_index("r1_en") == dehum_rule_key(1, RF_EN));
    CHECK(dehum_key_index("r4_action") == dehum_rule_key(4, RF_ACTION));
    CHECK(dehum_key_index("r4_action") == DEHUM_NKEYS - 1);
    CHECK(dehum_key_index("r5_en") == -1);
    CHECK(dehum_key_index("r1_match") == -1);     // reported, never set
    CHECK(dehum_key_index("dehum_rh") == -1);     // removed with the rules
    CHECK(dehum_key_index("mode") == -1);
    int n, f;
    REQUIRE(dehum_rule_of(dehum_key_index("r3_value"), n, f));
    CHECK(n == 3);
    CHECK(f == RF_VALUE);
    CHECK_FALSE(dehum_rule_of(DK_CYCLE_OFF, n, f));
}

TEST_CASE("ranges: the plain settings, and a cycle run never under min run") {
    DehumValues v;
    int lo, hi;
    REQUIRE(dehum_range(DK_TIMER_MIN, v, lo, hi)); CHECK(lo == 1); CHECK(hi == 1440);
    REQUIRE(dehum_range(DK_CYCLE_OFF, v, lo, hi)); CHECK(lo == 1); CHECK(hi == 240);
    REQUIRE(dehum_range(DK_MIN_RUN, v, lo, hi));   CHECK(lo == 0); CHECK(hi == 60);
    REQUIRE(dehum_range(DK_CYCLE_ON, v, lo, hi));  CHECK(lo == 1); CHECK(hi == 240);
    v.v[DK_MIN_RUN] = 15; v.known[DK_MIN_RUN] = true;
    REQUIRE(dehum_range(DK_CYCLE_ON, v, lo, hi));  CHECK(lo == 15);
    v.v[DK_MIN_RUN] = 0;
    REQUIRE(dehum_range(DK_CYCLE_ON, v, lo, hi));  CHECK(lo == 1);
    CHECK(dehum_step(DK_TIMER_MIN) == 15);
    CHECK(dehum_step(DK_CYCLE_ON) == 5);
    CHECK(dehum_step(DK_MIN_RUN) == 1);
    CHECK(dehum_step(dehum_rule_key(2, RF_VALUE)) == 1);
    CHECK_FALSE(dehum_range(-1, v, lo, hi));
    CHECK_FALSE(dehum_range(DEHUM_NKEYS, v, lo, hi));
}

TEST_CASE("ranges: a rule's threshold follows its sensor") {
    DehumValues v;
    const int sensor = dehum_rule_key(2, RF_SENSOR), value = dehum_rule_key(2, RF_VALUE);
    int lo, hi;
    v.known[sensor] = true; v.v[sensor] = DEHUM_SENSOR_TEMP;
    REQUIRE(dehum_range(value, v, lo, hi)); CHECK(lo == -20); CHECK(hi == 50);
    v.v[sensor] = DEHUM_SENSOR_HUM;
    REQUIRE(dehum_range(value, v, lo, hi)); CHECK(lo == 0); CHECK(hi == 100);
    v.known[sensor] = false;                  // unknown: the app's humidity default
    REQUIRE(dehum_range(value, v, lo, hi)); CHECK(lo == 0); CHECK(hi == 100);
    REQUIRE(dehum_range(dehum_rule_key(2, RF_RELAY), v, lo, hi)); CHECK(lo == 1); CHECK(hi == 2);
    for (int f : {RF_EN, RF_SENSOR, RF_CMP, RF_ACTION}) {
        REQUIRE(dehum_range(dehum_rule_key(2, f), v, lo, hi));
        CHECK(lo == 0);
        CHECK(hi == 1);
    }
}

TEST_CASE("format and labels") {
    DehumValues v;
    char b[32];
    dehum_format(DK_TIMER_MIN, 120, true, v, b, sizeof b);     CHECK(std::string(b) == "120 min");
    dehum_format(DK_TIMER_MIN, 0, false, v, b, sizeof b);      CHECK(std::string(b) == "--");
    const int s1 = dehum_rule_key(1, RF_SENSOR);
    v.known[s1] = true; v.v[s1] = DEHUM_SENSOR_TEMP;
    dehum_format(dehum_rule_key(1, RF_VALUE), -5, true, v, b, sizeof b);
    CHECK(std::string(b) == "-5 °C");
    v.v[s1] = DEHUM_SENSOR_HUM;
    dehum_format(dehum_rule_key(1, RF_VALUE), 65, true, v, b, sizeof b);
    CHECK(std::string(b) == "65 %RH");
    dehum_format(dehum_rule_key(1, RF_RELAY), 2, true, v, b, sizeof b);
    CHECK(std::string(b) == "2");
    dehum_label(DK_CYCLE_ON, b, sizeof b);                    CHECK(std::string(b) == "Cycle run");
    dehum_label(dehum_rule_key(2, RF_VALUE), b, sizeof b);    CHECK(std::string(b) == "Rule 2 threshold");
    dehum_label(dehum_rule_key(4, RF_CMP), b, sizeof b);      CHECK(std::string(b) == "Rule 4 above/below");
    dehum_label(dehum_rule_key(1, RF_ACTION), b, sizeof b);   CHECK(std::string(b) == "Rule 1 on/off");
    dehum_label(dehum_rule_key(3, RF_EN), b, sizeof b);       CHECK(std::string(b) == "Rule 3 enabled");
}

// ------------------------------------------------------------------ status text
static std::string mode_text(DehumState s) { char b[48]; return dehum_mode_text(s, b, sizeof b); }

TEST_CASE("status words: modes, with time left rounded up to the minute") {
    DehumState s;
    CHECK(mode_text(s) == "--");                              // before any data
    s = parsed("dehum_data_timer.json");                      // 4980 s = 83 min
    CHECK(mode_text(s) == "TIMER 1 h 23 m left");
    s.remain_s = 3600;  CHECK(mode_text(s) == "TIMER 1 h left");
    s.remain_s = 61;    CHECK(mode_text(s) == "TIMER 2 m left");
    s.remain_s = 0;     CHECK(mode_text(s) == "TIMER 0 m left");
    s.mode = DehumMode::Cycle; s.phase = 1; s.remain_s = 720;
    CHECK(mode_text(s) == "CYCLE run 12 m left");
    s.phase = 0; s.remain_s = 300;
    CHECK(mode_text(s) == "CYCLE rest 5 m left");
    s.mode = DehumMode::Auto;    CHECK(mode_text(s) == "AUTO (rules)");
    s.mode = DehumMode::On;      CHECK(mode_text(s) == "ON");
    s.mode = DehumMode::Off;     CHECK(mode_text(s) == "OFF");
    s.mode = DehumMode::Unknown; CHECK(mode_text(s) == "--");
}

TEST_CASE("status words: which rules are met (AUTO only) and both relays") {
    char b[64];
    DehumState s = parsed("dehum_data_rules.json");
    CHECK(std::string(dehum_auto_text(s, b, sizeof b)) == "rules met: 1, 4");
    s.rule_match[0] = s.rule_match[3] = false;
    CHECK(std::string(dehum_auto_text(s, b, sizeof b)) == "no rule met");
    s.rule_match[1] = true;
    CHECK(std::string(dehum_auto_text(s, b, sizeof b)) == "rules met: 2");
    s.mode = DehumMode::On;
    CHECK(std::string(dehum_auto_text(s, b, sizeof b)) == "");
    CHECK(std::string(dehum_relays_text(s, b, sizeof b)) == "Relay 1 on • Relay 2 on");
    s.relay = false; s.relay2 = false;
    CHECK(std::string(dehum_relays_text(s, b, sizeof b)) == "Relay 1 off • Relay 2 off");
    DehumState none;
    CHECK(std::string(dehum_relays_text(none, b, sizeof b)) == "");
    CHECK(std::string(dehum_status_line(none, b, sizeof b)) == "--");
}

TEST_CASE("status line joins the non-empty parts") {
    char b[128];
    const DehumState a = parsed("dehum_data_auto.json");
    CHECK(std::string(dehum_status_line(a, b, sizeof b)) ==
          "AUTO (rules)  •  Relay 1 on • Relay 2 off  •  rules met: 1");
    const DehumState t = parsed("dehum_data_timer.json");
    CHECK(std::string(dehum_status_line(t, b, sizeof b)) ==
          "TIMER 1 h 23 m left  •  Relay 1 on • Relay 2 off");
}

// ------------------------------------------------------------------ health
TEST_CASE("health: broker first, then offline, waiting, silent, online") {
    DehumState s;
    const int64_t t = 1700000000;
    CHECK(dehum_health(LINK_MQTT_DOWN, s, t) == DehumView::BrokerDown);
    CHECK(dehum_health(LINK_WIFI_DOWN, s, t) == DehumView::BrokerDown);
    CHECK(dehum_health(LINK_OK, s, t) == DehumView::Waiting);
    dehum_parse_availability(s, "offline", 7);
    CHECK(dehum_health(LINK_OK, s, t) == DehumView::Offline);
    s = parsed("dehum_data_auto.json", t);
    CHECK(dehum_health(LINK_OK, s, t) == DehumView::Online);   // data without availability yet
    dehum_parse_availability(s, "offline", 7);
    CHECK(dehum_health(LINK_OK, s, t) == DehumView::Offline);   // the LWT wins over old data
    dehum_parse_availability(s, "online", 6);
    CHECK(dehum_health(LINK_OK, s, t + 90) == DehumView::Online);
    CHECK(dehum_health(LINK_OK, s, t + 91) == DehumView::Silent);
    CHECK(dehum_health(LINK_MQTT_DOWN, s, t) == DehumView::BrokerDown);
    CHECK(std::string(dehum_view_text(DehumView::Silent)) == "Dehumidifier silent: no data for 90 s");
    CHECK(std::string(dehum_view_text(DehumView::Waiting)) == "Waiting for dehumidifier");
    CHECK(std::string(dehum_view_text(DehumView::Offline)) == "Dehumidifier offline");
    CHECK(std::string(dehum_view_text(DehumView::BrokerDown)) == "Broker unreachable");
}

// ------------------------------------------------------------------ commands
static std::string toast(DehumCommands& c) {
    char b[96];
    return c.take_toast(b, sizeof b) ? std::string(b) : std::string();
}

TEST_CASE("commands: a step debounces, then one integer goes to dehum/cmd unretained") {
    test_publish_reset(false);
    DehumCommands c;
    const DehumState s = parsed("dehum_data_auto.json");
    REQUIRE(c.step(DK_MIN_RUN, +1, s, true, 1000));
    int v;
    REQUIRE(c.display_value(DK_MIN_RUN, s, v));
    CHECK(v == 11);                      // shown at once
    CHECK(c.is_pending(DK_MIN_RUN));
    c.tick(1399, s, true);
    CHECK(test_publishes().empty());
    c.tick(1400, s, true);
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].topic == "dehum/cmd");
    CHECK(test_publishes()[0].payload == "{\"min_run\":11}");
    CHECK_FALSE(test_publishes()[0].retain);
}

TEST_CASE("commands: taps inside the debounce send only the last value") {
    test_publish_reset(false);
    DehumCommands c;
    const DehumState s = parsed("dehum_data_auto.json");
    const int thr = dehum_rule_key(2, RF_VALUE);    // 55 %RH
    c.step(thr, -1, s, true, 1000);
    c.step(thr, -1, s, true, 1100);
    c.step(thr, -1, s, true, 1200);
    c.tick(1500, s, true);
    CHECK(test_publishes().empty());
    c.tick(1600, s, true);
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].payload == "{\"r2_value\":52}");
}

TEST_CASE("commands: steps stay in the live range; at an end nothing is queued") {
    test_publish_reset(false);
    DehumCommands c;
    DehumState s = parsed("dehum_data_auto.json");
    // Timer steps 15 and stops at 1440.
    s.settings.v[DK_TIMER_MIN] = 1435;
    REQUIRE(c.step(DK_TIMER_MIN, +1, s, true, 0));
    int v; c.display_value(DK_TIMER_MIN, s, v);
    CHECK(v == 1440);
    CHECK_FALSE(c.step(DK_TIMER_MIN, +1, s, true, 0));
    // A cycle run cannot go below min run (10).
    s.settings.v[DK_CYCLE_ON] = 12;
    REQUIRE(c.step(DK_CYCLE_ON, -1, s, true, 0));
    c.display_value(DK_CYCLE_ON, s, v);
    CHECK(v == 10);
    // A temperature rule's threshold stops at -20.
    const int r3v = dehum_rule_key(3, RF_VALUE);
    s.settings.v[r3v] = -20;
    CHECK_FALSE(c.step(r3v, -1, s, true, 0));
    // Unknown values cannot be stepped.
    s.settings.known[DK_CYCLE_OFF] = false;
    CHECK_FALSE(c.step(DK_CYCLE_OFF, +1, s, true, 0));
}

TEST_CASE("commands: a threshold's range follows the sensor just picked") {
    test_publish_reset(false);
    DehumCommands c;
    const DehumState s = parsed("dehum_data_auto.json");   // r1: humidity, 65
    const int sensor = dehum_rule_key(1, RF_SENSOR), value = dehum_rule_key(1, RF_VALUE);
    CHECK(c.enter(value, 90, s, true, 0));                 // fine for humidity
    REQUIRE(c.enter(sensor, DEHUM_SENSOR_TEMP, s, true, 0));
    const DehumValues cur = c.display_values(s);
    int lo, hi;
    REQUIRE(dehum_range(value, cur, lo, hi));
    CHECK(hi == 50);
    CHECK_FALSE(c.enter(value, 90, s, true, 0));            // not for temperature
    CHECK(c.enter(value, -10, s, true, 0));
    CHECK_FALSE(c.enter(dehum_rule_key(1, RF_RELAY), 3, s, true, 0));
    CHECK_FALSE(c.enter(-1, 0, s, true, 0));
}

TEST_CASE("commands: nothing is queued or sent while the dehumidifier is not online") {
    test_publish_reset(false);
    DehumCommands c;
    const DehumState s = parsed("dehum_data_auto.json");
    CHECK_FALSE(c.step(DK_MIN_RUN, +1, s, false, 0));
    CHECK_FALSE(c.enter(dehum_rule_key(2, RF_EN), 1, s, false, 0));
    CHECK_FALSE(c.is_pending(DK_MIN_RUN));
    CHECK_FALSE(c.set_mode(DehumMode::On, false));
    CHECK(test_publishes().empty());
    CHECK(toast(c) == "Couldn't send: dehumidifier not reachable");
    // Queued while online, then the dehumidifier drops before the flush.
    REQUIRE(c.step(DK_MIN_RUN, +1, s, true, 0));
    c.tick(500, s, false);
    CHECK(test_publishes().empty());
    CHECK_FALSE(c.is_pending(DK_MIN_RUN));
    CHECK(toast(c) == "Couldn't send: dehumidifier not reachable");
}

TEST_CASE("commands: mode buttons publish at once and are not pending") {
    test_publish_reset(false);
    DehumCommands c;
    CHECK(c.set_mode(DehumMode::Timer, true));
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].topic == "dehum/cmd");
    CHECK(test_publishes()[0].payload == "{\"mode\":\"timer\"}");
    CHECK_FALSE(test_publishes()[0].retain);
    CHECK(toast(c).empty());
    test_publish_reset(true);
    CHECK_FALSE(c.set_mode(DehumMode::Auto, true));
    CHECK(toast(c) == "Couldn't send: dehumidifier not reachable");
    CHECK_FALSE(c.set_mode(DehumMode::Unknown, true));
}

TEST_CASE("commands: a failed publish reverts with the not-sent notice") {
    test_publish_reset(true);
    DehumCommands c;
    const DehumState s = parsed("dehum_data_auto.json");
    REQUIRE(c.step(DK_MIN_RUN, +1, s, true, 0));
    c.tick(400, s, true);
    CHECK_FALSE(c.is_pending(DK_MIN_RUN));
    int v; c.display_value(DK_MIN_RUN, s, v);
    CHECK(v == 10);
    CHECK(toast(c) == "Couldn't send: dehumidifier not reachable");
}

TEST_CASE("commands: pending until dehum/data reflects it, not merely until data arrives") {
    test_publish_reset(false);
    DehumCommands c;
    DehumState s = parsed("dehum_data_auto.json");
    const int en = dehum_rule_key(2, RF_EN);
    REQUIRE(c.enter(en, 1, s, true, 0));
    c.tick(400, s, true);
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].payload == "{\"r2_en\":1}");
    c.on_state(s);                       // the periodic publish, still r2_en 0
    CHECK(c.is_pending(en));
    s.settings.v[en] = 1;
    c.on_state(s);                       // the echo
    CHECK_FALSE(c.is_pending(en));
    CHECK(toast(c).empty());
}

TEST_CASE("commands: a clamped answer still clears") {
    test_publish_reset(false);
    DehumCommands c;
    DehumState s = parsed("dehum_data_auto.json");
    REQUIRE(c.enter(DK_CYCLE_ON, 12, s, true, 0));
    c.tick(400, s, true);
    s.settings.v[DK_CYCLE_ON] = 15;       // min_run moved under us; the box clamped
    c.on_state(s);
    CHECK_FALSE(c.is_pending(DK_CYCLE_ON));
    int v; c.display_value(DK_CYCLE_ON, s, v);
    CHECK(v == 15);
}

TEST_CASE("commands: unanswered edits time out with the app's notice") {
    test_publish_reset(false);
    DehumCommands c;
    const DehumState s = parsed("dehum_data_auto.json");
    REQUIRE(c.step(dehum_rule_key(2, RF_VALUE), +1, s, true, 0));
    REQUIRE(c.step(DK_TIMER_MIN, +1, s, true, 0));
    c.tick(400, s, true);
    REQUIRE(test_publishes().size() == 2);
    c.tick(3399, s, true);
    CHECK(c.is_pending(DK_TIMER_MIN));
    c.tick(3400, s, true);
    CHECK_FALSE(c.is_pending(DK_TIMER_MIN));
    CHECK_FALSE(c.is_pending(dehum_rule_key(2, RF_VALUE)));
    CHECK(toast(c) == "Not applied: Timer, Rule 2 threshold");
    CHECK(toast(c).empty());            // one-shot
}

TEST_CASE("commands: stepping back to the reported value sends nothing") {
    test_publish_reset(false);
    DehumCommands c;
    const DehumState s = parsed("dehum_data_auto.json");
    REQUIRE(c.step(DK_MIN_RUN, +1, s, true, 0));
    REQUIRE(c.step(DK_MIN_RUN, -1, s, true, 100));
    c.tick(600, s, true);
    CHECK(test_publishes().empty());
    CHECK_FALSE(c.is_pending(DK_MIN_RUN));
    CHECK(toast(c).empty());
}

TEST_CASE("commands: re-editing a value in flight is sent even when it is back to the reported one") {
    test_publish_reset(false);
    DehumCommands c;
    const DehumState s = parsed("dehum_data_auto.json");
    REQUIRE(c.step(DK_MIN_RUN, +1, s, true, 0));
    c.tick(400, s, true);                      // {"min_run":11} sent
    REQUIRE(c.step(DK_MIN_RUN, -1, s, true, 500));
    c.tick(900, s, true);
    REQUIRE(test_publishes().size() == 2);
    CHECK(test_publishes()[1].payload == "{\"min_run\":10}");
}
