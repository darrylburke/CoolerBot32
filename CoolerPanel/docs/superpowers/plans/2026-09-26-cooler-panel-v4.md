# CoolerPanel v4 (single-relay controller) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the CoolerPanel from the retired v3 dual-AC controller to the v4 single-relay controller's `/data` v2 contract. That means a single-unit Trend screen, tabbed Settings, calibration controls on Detail, and the new alarms.

**Architecture:** The model layer (`shared/model/`) is rebuilt around the v2 payload first. The v3 fields stay as an inert, marked legacy block so every task builds, then the last task deletes them, which proves nothing still reads them. Text formatting is pulled into a pure, unit-tested `status_text` module. The screens are then rewritten one per task and verified with headless sim screenshots driven by a new `--fixture` flag.

**Tech Stack:** C++17, LVGL 9.3.0, ArduinoJson 7.4.3, doctest 2.4.11, CMake, SDL2 sim. The device firmware uses PlatformIO (pioarduino, Arduino core 3.2.0) on a Waveshare ESP32-S3-Touch-LCD-4B.

**Spec:** `~/Cooler/docs/superpowers/specs/2026-09-26-single-relay-controller-design.md` §3 (MQTT contract) and §4 (panel changes). This plan deviates from §4.2 in two ways, both forced by the current code and noted in the tasks:
- **Trend:** it adapts the uncommitted "Quiet Slate" Trend redesign already in the working tree (three-column grid) instead of the older spec sketch.
- **Settings:** it is split into three tabs, because ten settings don't fit one fixed, non-scrolling 480 px page.

**Companion plan:** `~/Cooler/docs/superpowers/plans/2026-09-26-single-relay-controller.md` builds the controller that publishes this contract. The panel can be built and fully verified without it, using the fixtures. Task 9's live check needs the controller running.

**Provenance:** every code block and patch here was applied in order to a scratch copy of this repo (baseline = the current working tree including its uncommitted work). After each task it built, `ctest` passed (unit + portability_guard), and the screenshots described were taken and inspected. The device firmware (`pio run`) also built at the end.

## Global Constraints

- `/data` payloads without `"v": 2` are rejected whole (a stale retained v3 payload must never be half-parsed).
- JSON `null` for a sensor value means "invalid": keep the last number and clear `*_valid`. A missing key means "unchanged".
- Settings bounds match the controller exactly, except the existing panel-side divergences: `dutypercent` floors at 5 with step 5, and `sampleinterval` uses preset chips.
- `fin_recover`'s floor is `fin_cutoff + 1`, tracked live.
- Override is **not** commandable from the panel (it is a physical switch or link loss). Its thresholds are 3 / 5 °C. The panel displays it and never sends it.
- Everything under `shared/` may call LVGL and nothing else (`tests/portability_guard.sh`).
- `config/cooler_defaults.h` and `device/src/assets_gen/` are git-ignored (they contain credentials). Never add them.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

Inputs the spec implies but doesn't spell out, each pinned by a test in the task that owns it:

1. **A stale retained v3 `/data` on the broker** at first boot, before the new controller has published: it must be ignored, not half-parsed into zeros. Test: "a v3 payload (no schema version) is rejected" (Task 1).
2. **`temp: null` for hours** (SHT30 dead): the trend must show a gap, not a flat line at the last value, and NotKeepingUp must not fire on a stale number. Tests: "a /data with temp:null does not add a history sample" (Task 1); "a null temperature never counts as not keeping up" (Task 3).
3. **Negative settings** (`fin_cutoff` −2 °C) through the stepper, the `{"k":%d}` publish and the reconcile path. Test: "negative settings are published and reconciled" (Task 2).
4. **Override with a far-off `coolerset`** (e.g. 20): the alarm and chart band must use the fixed 5 °C, not coolerset + range. Test: "in override, not-keeping-up is measured against the fixed 5 C" (Task 3). The chart band is checked by the override screenshot (Task 5).
5. **A failed calibration command on the Detail page:** Commands' toast only renders on Settings, so Detail must show its own failure. There's no unit test (LVGL). Task 7 Step 4 checks it by hand with the broker unreachable.

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `shared/model/cooler_state.{h,cpp}` | rewrite | v2 `/data` mirror, schema gate, null handling, `cooler_in_override()` |
| `shared/model/bounds.{h,cpp}` | rewrite | settable values, bounds, tab `Group`, `fin_recover` floor |
| `shared/model/commands.{h,cpp}` | modify | new keys, `fin_cutoff`-aware clamp, fire-and-forget `action()` |
| `shared/model/alarm.{h,cpp}` | modify | NoResponse / BoxSensorFault / FinSensorFault, override-aware NotKeepingUp |
| `shared/model/status_text.{h,cpp}` | new | pure text for mode/state/compressor/calibration |
| `shared/app.cpp` | modify | history sampled only on a valid temp; records the relay state |
| `shared/ui/screen_trend.cpp` | rewrite | single-unit cards, override-aware band |
| `shared/ui/screen_settings.{h,cpp}` | rewrite | Box / Coil / Timing tabs |
| `shared/ui/screen_detail.cpp` | rewrite | v2 diagnostics and calibration controls |
| `shared/ui/screen_alarm.cpp` | modify | wrapping titles, per-alarm detail text |
| `shared/ui/nav.{h,cpp}` | modify | `nav_show_page()` for the sim |
| `sim/main.cpp`, `sim/fixture_feed.cpp`, `sim/seed_history.cpp` | modify / new | `--fixture`, `--page`, `--tab`; single-relay seed |
| `tests/fixtures/data_*.json` | replace | one v2 payload per controller state, plus `data_v3.json` |
| `tests/test_*.cpp` | modify / new | per task |

## Build and screenshot commands (used by every task)

- **BUILD** (reuses the already-downloaded dependencies; `build/` is git-ignored and its old cache was configured from another path, so this uses `build/v4`):

```bash
cd ~/Cooler/CoolerPanel && cmake -S . -B build/v4 -DFETCHCONTENT_SOURCE_DIR_LVGL=$PWD/build/_deps/lvgl-src -DFETCHCONTENT_SOURCE_DIR_ARDUINOJSON=$PWD/build/_deps/arduinojson-src -DFETCHCONTENT_SOURCE_DIR_DOCTEST=$PWD/build/_deps/doctest-src > /dev/null && cmake --build build/v4 -j && ctest --test-dir build/v4 --output-on-failure
```

- **SHOT** prefix for headless screenshots (1100 frames ≈ 5.5 s, enough to clear the 4.5 s boot splash):

```bash
cd ~/Cooler/CoolerPanel && SDL_VIDEODRIVER=dummy ./build/v4/cooler_sim --fixture tests/fixtures/<name>.json [--seed-history] [--page N] [--tab N] --frames 1100 --screenshot /tmp/<out>.png
```

Open each PNG and compare it to the expected description in the step. `--page`/`--tab` exist from Task 4 and Task 6.

---

### Task 0: Commit the in-progress work you already have (needs your OK)

The working tree carries about 600 lines of uncommitted work: the "Quiet Slate" Trend redesign, per-unit chart colouring, humidity bands, the on-panel WiFi/MQTT form, and icons. This plan's patches are made against that state. It builds and passes `ctest` as-is.

- [ ] **Step 1: Confirm with the user** that this work should be committed as one commit before v4 work starts. If they say no, stop. The patches below will not apply to any other state.
- [ ] **Step 2: Verify it builds and passes**

Run: BUILD
Expected: `100% tests passed, 0 tests failed out of 2`

- [ ] **Step 3: Commit it**

```bash
cd ~/Cooler/CoolerPanel
git add -A
git reset -q tools/__pycache__ 2>/dev/null || true
git status --short        # must NOT list config/cooler_defaults.h or device/src/assets_gen/
git commit -m "feat: Quiet Slate trend redesign, per-unit bars, humidity band, on-panel config form

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 1: v2 state model, fixtures, history gating

**Files:**
- Rewrite: `shared/model/cooler_state.h`, `shared/model/cooler_state.cpp`, `tests/test_state.cpp`
- Replace fixtures: `git mv tests/fixtures/data_normal.json tests/fixtures/data_v3.json`, `git rm tests/fixtures/data_fault.json`, then write the seven v2 fixtures below (`data_override.json` is overwritten)
- Modify: `shared/app.cpp`, `tests/test_app_wiring.cpp`

**Interfaces:**
- Produces: `struct CoolerState` v2 fields (see header). Each nullable reading has a `*_valid` flag: `temp_valid`, `humidity_valid`, `fin_temp_valid`, `fin_cal_err_valid`. `compressor` is −1 when unknown.
- Produces: `bool cooler_parse_data(CoolerState&, const char*, size_t, int64_t)` (unchanged signature, v2 semantics), and new `bool cooler_in_override(const CoolerState&)`.
- A marked `LEGACY v3` block keeps the old field names (always zero) so the untouched screens still compile. Task 8 deletes it.

- [ ] **Step 1: Swap the fixtures**

```bash
cd ~/Cooler/CoolerPanel
git mv tests/fixtures/data_normal.json tests/fixtures/data_v3.json
git rm -q tests/fixtures/data_fault.json
```

Then write these files (each is a single line of JSON):

`tests/fixtures/data_normal.json`:

```json
{"v":2,"temp":4.8,"humidity":78.2,"fin_temp":1.6,"fin_ohms":28410,"fin_slope":-0.8,"mode":"normal","override_src":"none","state":"cooling","relay":1,"cool_call":1,"defrost":0,"compressor":1,"sht_fault":0,"fin_fault":0,"no_response":0,"run_s":142,"off_s":0,"hold_s":38,"coolerset":4,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,"sampleinterval":3600,"fin_cutoff":0,"fin_recover":3,"settle":10,"fin_cal":1,"fin_beta":3912,"fin_r0":10240,"fin_cal_err":0.3,"cal_active":0,"cal_points":0,"cal_span":0.0,"hist_n":3,"hist_interval_s":3600,"hist_last_ts":1790000000,"temp_hist":[6,5,5],"hum_hist":[80,79,78],"uptime_s":86400}
```

`tests/fixtures/data_defrost.json`:

```json
{"v":2,"temp":4.8,"humidity":78.2,"fin_temp":-0.4,"fin_ohms":35900,"fin_slope":0.6,"mode":"normal","override_src":"none","state":"defrost","relay":0,"cool_call":1,"defrost":1,"compressor":0,"sht_fault":0,"fin_fault":0,"no_response":0,"run_s":0,"off_s":75,"hold_s":0,"coolerset":4,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,"sampleinterval":3600,"fin_cutoff":0,"fin_recover":3,"settle":10,"fin_cal":1,"fin_beta":3912,"fin_r0":10240,"fin_cal_err":0.3,"cal_active":0,"cal_points":0,"cal_span":0.0,"hist_n":3,"hist_interval_s":3600,"hist_last_ts":1790000000,"temp_hist":[6,5,5],"hum_hist":[80,79,78],"uptime_s":86400}
```

`tests/fixtures/data_override.json`:

```json
{"v":2,"temp":3.4,"humidity":78.2,"fin_temp":3.9,"fin_ohms":28410,"fin_slope":0.1,"mode":"override","override_src":"switch","state":"idle","relay":0,"cool_call":0,"defrost":0,"compressor":0,"sht_fault":0,"fin_fault":0,"no_response":0,"run_s":0,"off_s":900,"hold_s":0,"coolerset":4,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,"sampleinterval":3600,"fin_cutoff":0,"fin_recover":3,"settle":10,"fin_cal":1,"fin_beta":3912,"fin_r0":10240,"fin_cal_err":0.3,"cal_active":0,"cal_points":0,"cal_span":0.0,"hist_n":3,"hist_interval_s":3600,"hist_last_ts":1790000000,"temp_hist":[6,5,5],"hum_hist":[80,79,78],"uptime_s":86400}
```

`tests/fixtures/data_finproxy.json`:

```json
{"v":2,"temp":null,"humidity":null,"fin_temp":5.2,"fin_ohms":28410,"fin_slope":0.2,"mode":"fin-proxy","override_src":"none","state":"rest","relay":0,"cool_call":0,"defrost":0,"compressor":0,"sht_fault":1,"fin_fault":0,"no_response":0,"run_s":0,"off_s":240,"hold_s":360,"coolerset":4,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,"sampleinterval":3600,"fin_cutoff":0,"fin_recover":3,"settle":10,"fin_cal":1,"fin_beta":3912,"fin_r0":10240,"fin_cal_err":0.3,"cal_active":0,"cal_points":0,"cal_span":0.0,"hist_n":3,"hist_interval_s":3600,"hist_last_ts":1790000000,"temp_hist":[6,5,5],"hum_hist":[80,79,78],"uptime_s":86400}
```

`tests/fixtures/data_blind.json`:

```json
{"v":2,"temp":null,"humidity":null,"fin_temp":null,"fin_ohms":null,"fin_slope":null,"mode":"blind","override_src":"none","state":"rest","relay":0,"cool_call":1,"defrost":0,"compressor":null,"sht_fault":1,"fin_fault":1,"no_response":0,"run_s":0,"off_s":120,"hold_s":180,"coolerset":4,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,"sampleinterval":3600,"fin_cutoff":0,"fin_recover":3,"settle":10,"fin_cal":1,"fin_beta":3912,"fin_r0":10240,"fin_cal_err":0.3,"cal_active":0,"cal_points":0,"cal_span":0.0,"hist_n":3,"hist_interval_s":3600,"hist_last_ts":1790000000,"temp_hist":[6,5,5],"hum_hist":[80,79,78],"uptime_s":86400}
```

`tests/fixtures/data_noresponse.json`:

```json
{"v":2,"temp":12.1,"humidity":78.2,"fin_temp":11.8,"fin_ohms":28410,"fin_slope":0.0,"mode":"normal","override_src":"none","state":"cooling","relay":1,"cool_call":1,"defrost":0,"compressor":0,"sht_fault":0,"fin_fault":0,"no_response":1,"run_s":640,"off_s":0,"hold_s":0,"coolerset":4,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,"sampleinterval":3600,"fin_cutoff":0,"fin_recover":3,"settle":10,"fin_cal":1,"fin_beta":3912,"fin_r0":10240,"fin_cal_err":0.3,"cal_active":0,"cal_points":0,"cal_span":0.0,"hist_n":3,"hist_interval_s":3600,"hist_last_ts":1790000000,"temp_hist":[6,5,5],"hum_hist":[80,79,78],"uptime_s":86400}
```

`tests/fixtures/data_calibrating.json`:

```json
{"v":2,"temp":14.0,"humidity":78.2,"fin_temp":13.1,"fin_ohms":28410,"fin_slope":-0.8,"mode":"normal","override_src":"none","state":"idle","relay":0,"cool_call":0,"defrost":0,"compressor":0,"sht_fault":0,"fin_fault":0,"no_response":0,"run_s":0,"off_s":1500,"hold_s":0,"coolerset":4,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,"sampleinterval":3600,"fin_cutoff":0,"fin_recover":3,"settle":10,"fin_cal":0,"fin_beta":3950,"fin_r0":10000,"fin_cal_err":null,"cal_active":1,"cal_points":3,"cal_span":6.2,"hist_n":3,"hist_interval_s":3600,"hist_last_ts":1790000000,"temp_hist":[6,5,5],"hum_hist":[80,79,78],"uptime_s":86400}
```

- [ ] **Step 2: Write the failing tests**

`tests/test_state.cpp`:

```cpp
#include <doctest/doctest.h>
#include "cooler_state.h"
#include "mqtt_router.h"
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>

static std::string load(const char* name) {
    std::ifstream f(std::string(FIXture_DIR) + "/" + name);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

static CoolerState parsed(const char* name, int64_t now = 1000) {
    CoolerState s;
    auto j = load(name);
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), now));
    return s;
}

TEST_CASE("parses a v2 /data payload") {
    CoolerState s = parsed("data_normal.json", 1790000100);
    CHECK(s.valid);
    CHECK(s.last_rx_epoch == 1790000100);
    CHECK(s.coolerset == 4);
    CHECK(s.range == 2);
    CHECK(s.fin_cutoff == 0);
    CHECK(s.fin_recover == 3);
    CHECK(s.settle == 10);
    CHECK(s.minofftime == 5);
    CHECK(s.minruntime == 180);
    CHECK(s.maxrun == 10);
    CHECK(s.dutypercent == 50);
    CHECK(s.sampleinterval == 3600);
    CHECK(s.temp_valid);
    CHECK(s.temp == doctest::Approx(4.8));
    CHECK(s.humidity == doctest::Approx(78.2));
    CHECK(s.fin_temp_valid);
    CHECK(s.fin_temp == doctest::Approx(1.6));
    CHECK(s.fin_ohms == doctest::Approx(28410));
    CHECK(s.fin_slope == doctest::Approx(-0.8));
    CHECK(std::string(s.mode) == "normal");
    CHECK(std::string(s.override_src) == "none");
    CHECK(std::string(s.state) == "cooling");
    CHECK(s.relay == 1);
    CHECK(s.cool_call == 1);
    CHECK(s.compressor == 1);
    CHECK(s.run_s == 142u);
    CHECK(s.hold_s == 38u);
    CHECK(s.fin_cal == 1);
    CHECK(s.fin_beta == doctest::Approx(3912));
    CHECK(s.fin_cal_err_valid);
    CHECK(s.uptime_s == 86400u);
    CHECK_FALSE(cooler_in_override(s));
}

TEST_CASE("a v3 payload (no schema version) is rejected") {
    CoolerState s = parsed("data_normal.json", 100);
    auto v3 = load("data_v3.json");
    CHECK_FALSE(cooler_parse_data(s, v3.c_str(), v3.size(), 200));
    CHECK(s.last_rx_epoch == 100);
    CHECK(std::string(s.state) == "cooling");   // untouched
}

TEST_CASE("a payload with the wrong schema version is rejected") {
    CoolerState s;
    const char* v3ish = R"({"v":3,"temp":4.0})";
    CHECK_FALSE(cooler_parse_data(s, v3ish, std::strlen(v3ish), 1));
    CHECK_FALSE(s.valid);
}

TEST_CASE("malformed payload is rejected and leaves prior state intact") {
    CoolerState s = parsed("data_normal.json", 100);
    auto bad = load("data_malformed.json");
    CHECK_FALSE(cooler_parse_data(s, bad.c_str(), bad.size(), 200));
    CHECK(s.coolerset == 4);
    CHECK(s.last_rx_epoch == 100);
}

TEST_CASE("null sensor values mark invalid and keep the last reading") {
    CoolerState s = parsed("data_normal.json", 100);
    auto j = load("data_blind.json");
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), 200));
    CHECK_FALSE(s.temp_valid);
    CHECK(s.temp == doctest::Approx(4.8));        // last good value kept, not zeroed
    CHECK_FALSE(s.humidity_valid);
    CHECK_FALSE(s.fin_temp_valid);
    CHECK(s.compressor == -1);
    CHECK(s.sht_fault == 1);
    CHECK(s.fin_fault == 1);
    CHECK(std::string(s.mode) == "blind");
}

TEST_CASE("absent keys keep prior values; a later valid reading restores validity") {
    CoolerState s = parsed("data_blind.json", 100);
    const char* sparse = R"({"v":2,"coolerset":6})";
    REQUIRE(cooler_parse_data(s, sparse, std::strlen(sparse), 200));
    CHECK(s.coolerset == 6);
    CHECK(std::string(s.mode) == "blind");        // absent -> unchanged
    CHECK_FALSE(s.temp_valid);                     // absent -> unchanged
    const char* back = R"({"v":2,"temp":5.5})";
    REQUIRE(cooler_parse_data(s, back, std::strlen(back), 300));
    CHECK(s.temp_valid);
    CHECK(s.temp == doctest::Approx(5.5));
}

TEST_CASE("override fixtures") {
    CoolerState s = parsed("data_override.json");
    CHECK(cooler_in_override(s));
    CHECK(std::string(s.override_src) == "switch");
    const char* proxy = R"({"v":2,"mode":"override-proxy"})";
    REQUIRE(cooler_parse_data(s, proxy, std::strlen(proxy), 2));
    CHECK(cooler_in_override(s));
}

TEST_CASE("defrost, no-response and calibrating fixtures") {
    CoolerState d = parsed("data_defrost.json");
    CHECK(d.defrost == 1);
    CHECK(std::string(d.state) == "defrost");
    CHECK(d.fin_temp == doctest::Approx(-0.4));

    CoolerState n = parsed("data_noresponse.json");
    CHECK(n.no_response == 1);
    CHECK(n.relay == 1);
    CHECK(n.compressor == 0);

    CoolerState c = parsed("data_calibrating.json");
    CHECK(c.cal_active == 1);
    CHECK(c.cal_points == 3);
    CHECK(c.cal_span == doctest::Approx(6.2));
    CHECK(c.fin_cal == 0);
    CHECK_FALSE(c.fin_cal_err_valid);
}

TEST_CASE("fin-proxy fixture") {
    CoolerState s = parsed("data_finproxy.json");
    CHECK(std::string(s.mode) == "fin-proxy");
    CHECK(std::string(s.state) == "rest");
    CHECK(s.hold_s == 360u);
    CHECK_FALSE(s.temp_valid);
    CHECK(s.fin_temp_valid);
}

TEST_CASE("router dispatches data and availability") {
    router_set_prefix("ha/esp32-cooler");
    CoolerState s;
    auto j = load("data_normal.json");
    CHECK(route_message(s, "ha/esp32-cooler/data", j.c_str(), j.size(), 500));
    CHECK(s.coolerset == 4);

    CHECK(route_message(s, "ha/esp32-cooler/availability", "online", 6, 501));
    CHECK(s.online == true);
    CHECK(route_message(s, "ha/esp32-cooler/availability", "offline", 7, 502));
    CHECK(s.online == false);
}

TEST_CASE("router ignores unrelated and legacy per-unit topics") {
    router_set_prefix("ha/esp32-cooler");
    CoolerState s;
    CHECK_FALSE(route_message(s, "ha/esp32-cooler/ac1", "{}", 2, 0));
    CHECK_FALSE(route_message(s, "ha/other/data", "{}", 2, 0));
    CHECK_FALSE(route_message(s, "totally/unrelated", "{}", 2, 0));
}
```

Append to `tests/test_app_wiring.cpp`:

```cpp
TEST_CASE("a /data with temp:null does not add a history sample") {
    app_init_history();
    const std::string topic = std::string(router_prefix()) + "/data";
    const std::string blind = load("data_blind.json");
    app_on_mqtt_message(topic.c_str(), (const uint8_t*)blind.data(), blind.size());
    CHECK(panel_history().size() == 0);
}

TEST_CASE("the relay state is recorded with each sample") {
    app_init_history();
    const std::string topic = std::string(router_prefix()) + "/data";
    const std::string cooling = load("data_normal.json");     // relay 1
    app_on_mqtt_message(topic.c_str(), (const uint8_t*)cooling.data(), cooling.size());
    REQUIRE(panel_history().size() == 1);
    CHECK(panel_history().at(0).ac == 1);
}
```

- [ ] **Step 3: Run to verify it fails**

Run: BUILD
Expected: compile errors in `tests/test_state.cpp` (`'struct CoolerState' has no member named 'fin_cutoff'`, `'cooler_in_override' was not declared`).

- [ ] **Step 4: Implement**

`shared/model/cooler_state.h`:

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

// Mirrors the v4 single-relay controller's /data payload, schema "v": 2.
// Field meanings: docs/superpowers/specs/2026-09-26-single-relay-controller-design.md §3.1
// (in the parent Cooler directory).
struct CoolerState {
    // ---- settable config (spec §2.11) ----
    int coolerset = 0;
    int range = 0;
    int fin_cutoff = 0;      // C
    int fin_recover = 0;     // C
    int settle = 0;          // minutes
    int minofftime = 0;      // minutes
    int minruntime = 0;      // seconds
    int maxrun = 0;          // minutes
    int dutypercent = 0;
    int sampleinterval = 0;  // seconds

    // ---- live readings. *_valid is false while the controller publishes null
    //      (sensor failed); the value keeps its last good reading.
    float temp = 0.0f;       bool temp_valid = false;
    float humidity = 0.0f;   bool humidity_valid = false;
    float fin_temp = 0.0f;   bool fin_temp_valid = false;
    float fin_ohms = 0.0f;
    float fin_slope = 0.0f;  // C/min

    // ---- run state ----
    char mode[16] = {0};          // normal | override | fin-proxy | override-proxy | blind
    char override_src[8] = {0};   // none | switch | link | both
    char state[8] = {0};          // cooling | idle | defrost | wait | rest
    int relay = 0;
    int cool_call = 0;
    int defrost = 0;
    int compressor = -1;          // -1 unknown (null), 0 stopped, 1 running
    int sht_fault = 0;
    int fin_fault = 0;
    int no_response = 0;
    uint32_t run_s = 0, off_s = 0, hold_s = 0;

    // ---- fin calibration ----
    int fin_cal = 0;
    float fin_beta = 0.0f, fin_r0 = 0.0f;
    float fin_cal_err = 0.0f;  bool fin_cal_err_valid = false;
    int cal_active = 0, cal_points = 0;
    float cal_span = 0.0f;

    uint32_t uptime_s = 0;

    // ---- LEGACY v3 fields: no longer parsed, always zero. Kept only so the
    //      screens still compile until they are rewritten; the final task of
    //      the v4 panel plan deletes this block.
    int screentimeout = 0, override_on = 0, lead = 0, active_unit = 0, pot_pct = 0;
    bool ac_on = false, in_duty = false;
    char ac1[8] = {0}, ac2[8] = {0};
    bool ac1_relay = false, ac2_relay = false;
    int ac1_blocked = 0, ac2_blocked = 0;
    uint32_t ac1_runtime_s = 0, ac2_runtime_s = 0;
    int window_s = 0, window_pos_s = 0, window_on_s = 0, swap_in_s = 0;

    // ---- panel-side meta ----
    int64_t last_rx_epoch = 0;
    bool valid = false;
    bool online = false;     // from the availability topic
    // True once ANY /availability message (online or offline) has ever been
    // routed, independent of `valid` (which only tracks /data). A panel that
    // has never heard from the broker at all must stay silent; one that has
    // received a retained "offline" LWT but no /data yet must still alarm.
    bool availability_seen = false;
};

// Parse a /data payload. Rejects (returns false, leaves s untouched) anything
// that is not a JSON object carrying "v": 2 -- including a stale retained v3
// payload, which has no "v". now_epoch stamps last_rx_epoch.
bool cooler_parse_data(CoolerState& s, const char* json, size_t len, int64_t now_epoch);

// True in "override" and "override-proxy": the controller's fixed 5 / 3 C
// thermostat is in force and coolerset/range are not.
bool cooler_in_override(const CoolerState& s);
```

`shared/model/cooler_state.cpp`:

```cpp
#include "cooler_state.h"
#include <ArduinoJson.h>
#include <cstring>

static void copy_str(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = 0; return; }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

// Absent key: keep the prior value (and its validity). Explicit null: the
// controller is saying "no reading" -- mark invalid, keep the last number so
// nothing downstream ever sees a fabricated 0. isUnbound() is what tells the
// two apart (ArduinoJson 7: a missing key is unbound, a JSON null is bound).
static void read_opt(JsonObjectConst o, const char* k, float& out, bool& valid) {
    JsonVariantConst v = o[k];
    if (v.isUnbound()) return;
    if (v.is<float>() || v.is<int>()) { out = v.as<float>(); valid = true; }
    else if (v.isNull()) valid = false;
}

template <typename T>
static void read_num(JsonObjectConst o, const char* k, T& out) {
    JsonVariantConst v = o[k];
    if (v.is<T>()) out = v.as<T>();
}

static void read_str(JsonObjectConst o, const char* k, char* dst, size_t cap) {
    JsonVariantConst v = o[k];
    if (v.is<const char*>()) copy_str(dst, cap, v.as<const char*>());
}

bool cooler_parse_data(CoolerState& s, const char* json, size_t len, int64_t now_epoch) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    if (!doc.is<JsonObjectConst>()) return false;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    // Schema gate: only the v4 controller's payload. A retained v3 /data
    // (two-unit fields, no "v") must be ignored, not half-parsed.
    if (!o["v"].is<int>() || o["v"].as<int>() != 2) return false;

    read_num(o, "coolerset", s.coolerset);
    read_num(o, "range", s.range);
    read_num(o, "fin_cutoff", s.fin_cutoff);
    read_num(o, "fin_recover", s.fin_recover);
    read_num(o, "settle", s.settle);
    read_num(o, "minofftime", s.minofftime);
    read_num(o, "minruntime", s.minruntime);
    read_num(o, "maxrun", s.maxrun);
    read_num(o, "dutypercent", s.dutypercent);
    read_num(o, "sampleinterval", s.sampleinterval);

    read_opt(o, "temp", s.temp, s.temp_valid);
    read_opt(o, "humidity", s.humidity, s.humidity_valid);
    read_opt(o, "fin_temp", s.fin_temp, s.fin_temp_valid);
    bool unused;
    read_opt(o, "fin_ohms", s.fin_ohms, unused);
    read_opt(o, "fin_slope", s.fin_slope, unused);

    read_str(o, "mode", s.mode, sizeof(s.mode));
    read_str(o, "override_src", s.override_src, sizeof(s.override_src));
    read_str(o, "state", s.state, sizeof(s.state));
    read_num(o, "relay", s.relay);
    read_num(o, "cool_call", s.cool_call);
    read_num(o, "defrost", s.defrost);
    {
        JsonVariantConst c = o["compressor"];
        if (c.is<int>()) s.compressor = c.as<int>();
        else if (!c.isUnbound() && c.isNull()) s.compressor = -1;
    }
    read_num(o, "sht_fault", s.sht_fault);
    read_num(o, "fin_fault", s.fin_fault);
    read_num(o, "no_response", s.no_response);
    read_num(o, "run_s", s.run_s);
    read_num(o, "off_s", s.off_s);
    read_num(o, "hold_s", s.hold_s);

    read_num(o, "fin_cal", s.fin_cal);
    read_num(o, "fin_beta", s.fin_beta);
    read_num(o, "fin_r0", s.fin_r0);
    read_opt(o, "fin_cal_err", s.fin_cal_err, s.fin_cal_err_valid);
    read_num(o, "cal_active", s.cal_active);
    read_num(o, "cal_points", s.cal_points);
    read_num(o, "cal_span", s.cal_span);

    read_num(o, "uptime_s", s.uptime_s);

    s.last_rx_epoch = now_epoch;
    s.valid = true;
    return true;
}

bool cooler_in_override(const CoolerState& s) {
    return std::strncmp(s.mode, "override", 8) == 0;
}
```

Save this as `/tmp/task1-app.patch` and apply it from `CoolerPanel/` with `git apply /tmp/task1-app.patch` (it must apply cleanly; if it does not, the tree is not at the expected state -- stop and check the previous task):

````diff
diff --git a/shared/app.cpp b/shared/app.cpp
index 26fbc9d..13ca322 100644
--- a/shared/app.cpp
+++ b/shared/app.cpp
@@ -70,12 +70,13 @@ extern "C" void app_on_mqtt_message(const char* topic, const uint8_t* payload, s
     g_cmds.on_state(g_state);
     g_alarms.update(g_state, platform_epoch_utc());
 
-    // active_unit rides along so the trend can colour each bar by the unit
-    // that was running at the time -- the panel's own record of how the two
-    // units shared the load, which no single /data message can show.
-    if (g_state.valid)
+    // The relay state rides along so the trend can colour the bars where the
+    // controller was requesting cooling. Only a real box reading is sampled:
+    // while the SHT30 is down the controller publishes "temp":null, and the
+    // trend must show that as a gap, not as a flat line at the last value.
+    if (g_state.valid && g_state.temp_valid)
         g_hist.maybe_append(platform_epoch_utc(), g_state.temp, g_state.humidity, 30,
-                            (uint8_t)g_state.active_unit);
+                            (uint8_t)(g_state.relay ? 1 : 0));
 }
 
 extern "C" void app_set_link_state(link_state_t s) { g_link_state = s; }
````

- [ ] **Step 5: Run to verify it passes**

Run: BUILD
Expected: `100% tests passed, 0 tests failed out of 2`

- [ ] **Step 6: Commit**

```bash
cd ~/Cooler/CoolerPanel
git add shared/model/cooler_state.h shared/model/cooler_state.cpp shared/app.cpp tests/test_state.cpp tests/test_app_wiring.cpp tests/fixtures
git commit -m "feat(model): parse /data v2 from the single-relay controller

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Bounds and commands for the v2 settings

**Files:**
- Rewrite: `shared/model/bounds.h`, `shared/model/bounds.cpp`, `tests/test_bounds.cpp`, `shared/model/commands.cpp`, `tests/test_commands.cpp`
- Modify: `shared/model/commands.h`

**Interfaces:**
- Produces: `enum class Group { Box, Coil, Timing }` and `Bound::group`. The ten `BOUNDS[]` keys are `coolerset, range, sampleinterval, fin_cutoff, fin_recover, settle, minofftime, minruntime, maxrun, dutypercent`.
- Changes: `int bounds_clamp(const char* key, int v, int cur_fin_cutoff)`. The third parameter was `cur_minofftime`.
- Produces: `bool Commands::action(const char* key, int value)`, an immediate publish with no pending/reconcile. It returns false and sets the toast `"<key> failed"` if the publish fails.
- Removes: the `override` and `screentimeout` keys.

- [ ] **Step 1: Write the failing tests**

`tests/test_bounds.cpp`:

```cpp
#include <doctest/doctest.h>
#include "bounds.h"
#include <cstring>

TEST_CASE("every bound is well formed") {
    REQUIRE(BOUNDS_N == 10);
    for (size_t i = 0; i < BOUNDS_N; i++) {
        CHECK(BOUNDS[i].key != nullptr);
        CHECK(BOUNDS[i].label != nullptr);
        CHECK(BOUNDS[i].lo <= BOUNDS[i].hi);
    }
}

TEST_CASE("find locates keys and rejects unknowns and retired v3 keys") {
    REQUIRE(bounds_find("coolerset") != nullptr);
    CHECK(bounds_find("coolerset")->hi == 40);
    CHECK(bounds_find("nonsense") == nullptr);
    CHECK(bounds_find("screentimeout") == nullptr);
    CHECK(bounds_find("override") == nullptr);
}

TEST_CASE("clamp holds each value inside the controller's range") {
    CHECK(bounds_clamp("coolerset", 1, 0) == 2);
    CHECK(bounds_clamp("coolerset", 41, 0) == 40);
    CHECK(bounds_clamp("range", -1, 0) == 0);
    CHECK(bounds_clamp("range", 6, 0) == 5);
    CHECK(bounds_clamp("fin_cutoff", -6, 0) == -5);
    CHECK(bounds_clamp("fin_cutoff", 6, 0) == 5);
    CHECK(bounds_clamp("settle", 1, 0) == 2);
    CHECK(bounds_clamp("settle", 31, 0) == 30);
    CHECK(bounds_clamp("minofftime", 31, 0) == 30);
    CHECK(bounds_clamp("minruntime", -5, 0) == 0);
    CHECK(bounds_clamp("minruntime", 601, 0) == 600);
    CHECK(bounds_clamp("maxrun", 0, 0) == 1);
    CHECK(bounds_clamp("maxrun", 61, 0) == 60);
    CHECK(bounds_clamp("dutypercent", 1, 0) == 5);
    CHECK(bounds_clamp("dutypercent", 101, 0) == 100);
    CHECK(bounds_clamp("sampleinterval", 5, 0) == 10);
}

TEST_CASE("maxrun no longer depends on minofftime") {
    CHECK(bounds_clamp("maxrun", 3, 30) == 3);
}

TEST_CASE("fin_recover floor tracks the current fin_cutoff") {
    CHECK(bounds_clamp("fin_recover", 0, 0) == 1);
    CHECK(bounds_clamp("fin_recover", 3, 0) == 3);
    CHECK(bounds_clamp("fin_recover", 3, 4) == 5);
    CHECK(bounds_clamp("fin_recover", -4, -5) == -4);
    CHECK(bounds_clamp("fin_recover", 11, 0) == 10);
}

TEST_CASE("unknown keys pass through unchanged") {
    CHECK(bounds_clamp("nonsense", 12345, 0) == 12345);
}
```

`tests/test_commands.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

Run: BUILD
Expected: compile errors (`'class Commands' has no member named 'action'`) and, once that's past, `REQUIRE( BOUNDS_N == 10 )` failing.

- [ ] **Step 3: Implement**

`shared/model/bounds.h`:

```cpp
#pragma once
#include <stddef.h>

// How the settings screen renders this value.
enum class Widget { Stepper, Preset, Toggle };

// Which Settings tab the value lives on.
enum class Group { Box, Coil, Timing };

struct Bound {
    const char* key;     // MQTT command key
    const char* label;   // UI label
    const char* unit;    // display unit, "" for none
    int lo;
    int hi;
    int step;            // 0 for Preset/Toggle widgets
    Widget widget;
    Group group;
};

extern const Bound BOUNDS[];
extern const size_t BOUNDS_N;

// Returns nullptr if the key is not a settable value.
const Bound* bounds_find(const char* key);

// Clamp v into the legal range for key. cur_fin_cutoff supplies the dynamic
// floor for "fin_recover", which must stay at least 1 C above fin_cutoff.
// Unknown keys pass through unchanged.
int bounds_clamp(const char* key, int v, int cur_fin_cutoff);
```

`shared/model/bounds.cpp`:

```cpp
#include "bounds.h"
#include <cstring>

// Mirrors the v4 controller's settings table (spec §2.11). Two deliberate
// divergences, both panel-side only:
//   - dutypercent floors at 5 not 1 (a step-5 stepper from 1 is nonsense, and
//     sub-5 duty is moot once minruntime forces a floor on the actual run)
//   - sampleinterval uses preset chips, not a stepper
const Bound BOUNDS[] = {
    {"coolerset",      "Set point",    "C",    2,   40,  1, Widget::Stepper, Group::Box},
    {"range",          "Range +/-",    "C",    0,    5,  1, Widget::Stepper, Group::Box},
    {"sampleinterval", "Sample every", "s",   10, 3600,  0, Widget::Preset,  Group::Box},
    {"fin_cutoff",     "Ice cutoff",   "C",   -5,    5,  1, Widget::Stepper, Group::Coil},
    {"fin_recover",    "Ice clear",    "C",   -4,   10,  1, Widget::Stepper, Group::Coil},
    {"settle",         "Settle",       "min",  2,   30,  1, Widget::Stepper, Group::Coil},
    {"minofftime",     "Min off",      "min",  0,   30,  1, Widget::Stepper, Group::Timing},
    {"minruntime",     "Min run",      "s",    0,  600, 30, Widget::Stepper, Group::Timing},
    {"maxrun",         "Max run",      "min",  1,   60,  1, Widget::Stepper, Group::Timing},
    {"dutypercent",    "Backup duty",  "%",    5,  100,  5, Widget::Stepper, Group::Timing},
};
const size_t BOUNDS_N = sizeof(BOUNDS) / sizeof(BOUNDS[0]);

const Bound* bounds_find(const char* key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < BOUNDS_N; i++)
        if (std::strcmp(BOUNDS[i].key, key) == 0) return &BOUNDS[i];
    return nullptr;
}

int bounds_clamp(const char* key, int v, int cur_fin_cutoff) {
    const Bound* b = bounds_find(key);
    if (!b) return v;
    int lo = b->lo;
    if (std::strcmp(key, "fin_recover") == 0) {
        int dynamic_floor = cur_fin_cutoff + 1;
        if (dynamic_floor > lo) lo = dynamic_floor;
    }
    if (v < lo) return lo;
    if (v > b->hi) return b->hi;
    return v;
}
```

Save this as `/tmp/task2-commands-h.patch` and apply it from `CoolerPanel/` with `git apply /tmp/task2-commands-h.patch` (it must apply cleanly; if it does not, the tree is not at the expected state -- stop and check the previous task):

````diff
diff --git a/shared/model/commands.h b/shared/model/commands.h
index f4e6bd9..9f64f01 100644
--- a/shared/model/commands.h
+++ b/shared/model/commands.h
@@ -5,12 +5,12 @@
 
 static const uint32_t CMD_DEBOUNCE_MS = 400;
 static const uint32_t CMD_DEADLINE_MS = 3000;
-static const size_t   CMD_MAX_PENDING = 12;  // headroom over the 9 real settable keys
+static const size_t   CMD_MAX_PENDING = 12;  // headroom over the 10 settable keys
 
 class Commands {
 public:
     // Queue a change. Clamped against the bounds table using the last known
-    // minofftime. Repeated calls for the same key inside the debounce window
+    // fin_cutoff. Repeated calls for the same key inside the debounce window
     // coalesce into one publish of the final value.
     void request(const char* key, int value, uint32_t now_ms);
 
@@ -26,6 +26,12 @@ public:
     // otherwise the authoritative value from state.
     int display_value(const char* key, const CoolerState& s) const;
 
+    // Fire-and-forget action ({"calibrate":1}, {"fincal_reset":1}): published
+    // at once, no debounce and no pending/reconcile -- its effect shows up in
+    // later /data fields (cal_active, fin_cal) rather than as an echoed value.
+    // Returns false and raises a toast if the publish fails.
+    bool action(const char* key, int value);
+
     // One-shot failure notice. Returns false when there is nothing to show.
     bool take_toast(char* out, size_t n);
 
@@ -41,7 +47,7 @@ private:
     };
     Entry entries_[CMD_MAX_PENDING];
     CoolerState last_;         // most recent /data, for pre-values
-    int last_minofftime_ = 0;
+    int last_fin_cutoff_ = 0;
     char toast_[64] = {0};
     bool has_toast_ = false;
 
````

`shared/model/commands.cpp`:

```cpp
#include "commands.h"
#include "cooler_state.h"
#include "bounds.h"
#include "mqtt_router.h"
#include "platform.h"
#include <cstring>
#include <cstdio>

// Reads the live value of `key` out of state. Kept in one place so
// display_value() and on_state() cannot disagree about where a value lives.
static int state_value(const char* key, const CoolerState& s) {
    if (!std::strcmp(key, "coolerset"))      return s.coolerset;
    if (!std::strcmp(key, "range"))          return s.range;
    if (!std::strcmp(key, "fin_cutoff"))     return s.fin_cutoff;
    if (!std::strcmp(key, "fin_recover"))    return s.fin_recover;
    if (!std::strcmp(key, "settle"))         return s.settle;
    if (!std::strcmp(key, "minofftime"))     return s.minofftime;
    if (!std::strcmp(key, "minruntime"))     return s.minruntime;
    if (!std::strcmp(key, "maxrun"))         return s.maxrun;
    if (!std::strcmp(key, "dutypercent"))    return s.dutypercent;
    if (!std::strcmp(key, "sampleinterval")) return s.sampleinterval;
    return 0;
}

Commands::Entry* Commands::find(const char* key) {
    for (auto& e : entries_)
        if (e.active && !std::strcmp(e.key, key)) return &e;
    return nullptr;
}
const Commands::Entry* Commands::find(const char* key) const {
    for (const auto& e : entries_)
        if (e.active && !std::strcmp(e.key, key)) return &e;
    return nullptr;
}
Commands::Entry* Commands::alloc(const char* key) {
    if (Entry* e = find(key)) return e;
    for (auto& e : entries_) {
        if (!e.active) {
            e = Entry{};
            std::strncpy(e.key, key, sizeof(e.key) - 1);
            e.active = true;
            return &e;
        }
    }
    return nullptr;
}

void Commands::set_toast(const char* key) {
    std::snprintf(toast_, sizeof(toast_), "%s not acknowledged", key);
    has_toast_ = true;
}

void Commands::request(const char* key, int value, uint32_t now_ms) {
    Entry* e = alloc(key);
    if (!e) {
        // Table exhausted -- surface it rather than dropping the edit
        // silently. A ninth-plus concurrently-edited key must still be
        // visible as a failure, not vanish with no feedback at all.
        set_toast(key);
        return;
    }
    // pre is captured in tick(), immediately before THIS entry's specific
    // publish -- not here. Capturing it at request() time would let a
    // same-key re-request before the prior command's ack inherit a stale
    // pre (the value from before the FIRST command), which lets an
    // unrelated /data that merely echoes the first command's result get
    // misread as an answer to the second, still-unacknowledged one.
    e->want = bounds_clamp(key, value, last_fin_cutoff_);
    e->queued_ms = now_ms;
    e->sent = false;
}

void Commands::tick(uint32_t now_ms) {
    for (auto& e : entries_) {
        if (!e.active) continue;
        if (!e.sent) {
            if (now_ms - e.queued_ms < CMD_DEBOUNCE_MS) continue;
            e.pre = state_value(e.key, last_);   // value right before this publish
            char payload[64];
            int n = std::snprintf(payload, sizeof(payload), "{\"%s\":%d}", e.key, e.want);
            char topic[96];
            std::snprintf(topic, sizeof(topic), "%s/cmd", router_prefix());
            if (!platform_mqtt_publish(topic, payload, (size_t)n, false)) {
                set_toast(e.key);
                e.active = false;       // revert -- UI falls back to state
                continue;
            }
            e.sent = true;
            e.sent_ms = now_ms;
        } else if (now_ms - e.sent_ms > CMD_DEADLINE_MS) {
            set_toast(e.key);
            e.active = false;           // revert
        }
    }
}

void Commands::on_state(const CoolerState& s) {
    last_fin_cutoff_ = s.fin_cutoff;
    for (auto& e : entries_) {
        if (!e.active || !e.sent) continue;
        const int reported = state_value(e.key, s);
        // The cooler publishes /data every 30 s whether or not anything
        // changed, so "a message arrived" is NOT proof our command landed.
        // Clear only when this payload actually reflects it: the value moved
        // off what it was when we published, or it already equals what we
        // asked for. Otherwise stay pending and let the deadline fire --
        // without this, a dropped command reads as success.
        //
        // Clamping is still handled: we ask 50, the cooler answers 40, that
        // differs from pre, so we clear and display 40.
        if (reported != e.pre || reported == e.want) e.active = false;
    }
    last_ = s;
}

bool Commands::action(const char* key, int value) {
    char payload[64];
    int n = std::snprintf(payload, sizeof(payload), "{\"%s\":%d}", key, value);
    char topic[96];
    std::snprintf(topic, sizeof(topic), "%s/cmd", router_prefix());
    if (!platform_mqtt_publish(topic, payload, (size_t)n, false)) {
        std::snprintf(toast_, sizeof(toast_), "%s failed", key);
        has_toast_ = true;
        return false;
    }
    return true;
}

bool Commands::is_pending(const char* key) const {
    const Entry* e = find(key);
    return e != nullptr;
}

int Commands::display_value(const char* key, const CoolerState& s) const {
    if (const Entry* e = find(key)) return e->want;
    return state_value(key, s);
}

bool Commands::take_toast(char* out, size_t n) {
    if (!has_toast_) return false;
    std::strncpy(out, toast_, n - 1);
    out[n - 1] = 0;
    has_toast_ = false;
    return true;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: BUILD
Expected: `100% tests passed`. The Settings screen still renders its old single page, now with 9 stepper rows overflowing the bottom. That's expected until Task 6.

- [ ] **Step 5: Commit**

```bash
cd ~/Cooler/CoolerPanel
git add shared/model/bounds.h shared/model/bounds.cpp shared/model/commands.h shared/model/commands.cpp tests/test_bounds.cpp tests/test_commands.cpp
git commit -m "feat(model): v2 settings bounds, fin_recover floor, fire-and-forget actions

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Alarms for the v4 controller

**Files:**
- Modify: `shared/model/alarm.h`
- Rewrite: `shared/model/alarm.cpp`, `tests/test_alarm.cpp`

**Interfaces:**
- Changes: `enum class AlarmId { None, ControllerOffline, ControllerSilent, NoResponse, BoxSensorFault, FinSensorFault, NotKeepingUp, COUNT }`, in priority order. `SensorFault` is gone.
- `Alarms::text()` returns `"AC NOT RESPONDING"`, `"BOX SENSOR FAULT"` and `"COIL SENSOR FAULT"` for the new IDs.

- [ ] **Step 1: Write the failing tests**

`tests/test_alarm.cpp`:

```cpp
#include <doctest/doctest.h>
#include "alarm.h"
#include "cooler_state.h"
#include <cstdio>

static CoolerState ok_state() {
    CoolerState s;
    s.valid = true; s.online = true;
    // A fully healthy, fully-communicating panel has necessarily seen at
    // least one /availability message -- without this, every test below
    // that flips s.online = false to exercise ControllerOffline would
    // instead exercise "we've never heard an availability message at all"
    // (Step 4's safety fix), which is a different condition and stays
    // silent by design (see the two new test cases at the bottom of this
    // file).
    s.availability_seen = true;
    s.coolerset = 4; s.range = 2; s.temp = 4.0f; s.temp_valid = true;
    s.sht_fault = 0; s.last_rx_epoch = 1000;
    return s;
}

// Advance the clock WITH a fresh /data arrival. ControllerSilent outranks
// almost everything, so without refreshing last_rx_epoch it would fire
// incidentally in every test that advances time, masking the condition
// actually under test. The silence test below deliberately does not use this.
// Alarm-LOGIC tests pin their own thresholds. The shipped AlarmCfg defaults
// are a product-calibration decision (retuned against this cooler's measured
// pull-down curve) and must be free to change without breaking these.
static Alarms make_alarms(int over_c = 5, int over_s = 3600) {
    Alarms a;
    AlarmCfg c;                 // silent_s / holdoff_s keep their defaults
    c.over_c = over_c;
    c.over_s = over_s;
    a.configure(c);
    return a;
}

static void tick(Alarms& a, CoolerState& s, int64_t now) {
    s.last_rx_epoch = now;
    a.update(s, now);
}

TEST_CASE("healthy state raises nothing") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::None);
    CHECK_FALSE(a.any_latched());
}

TEST_CASE("offline availability fires immediately") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.online = false;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::ControllerOffline);
}

TEST_CASE("silence fires only after the timeout") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.last_rx_epoch = 1000;          // deliberately NOT refreshed
    a.update(s, 1299);
    CHECK(a.active(1299) == AlarmId::None);
    a.update(s, 1301);
    CHECK(a.active(1301) == AlarmId::ControllerSilent);
}

TEST_CASE("sensor fault fires immediately") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::BoxSensorFault);
}

TEST_CASE("not-keeping-up needs 5C over for 60 min continuously") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 12.0f;                  // coolerset 4 + range 2 + 5 = 11 threshold
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::None);        // clock just started
    tick(a, s, 4599);
    CHECK(a.active(4599) == AlarmId::None);        // 3599 s elapsed
    tick(a, s, 4601);
    CHECK(a.active(4601) == AlarmId::NotKeepingUp);
}

TEST_CASE("dropping back under the threshold resets the timer") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 12.0f;
    tick(a, s, 1000);
    tick(a, s, 4000);
    s.temp = 5.0f;                   // recovered
    tick(a, s, 4100);
    CHECK(a.active(4100) == AlarmId::None);
    s.temp = 12.0f;                  // over again -- clock restarts from 4200
    tick(a, s, 4200);
    tick(a, s, 4200 + 3599);
    CHECK(a.active(4200 + 3599) == AlarmId::None);
}

TEST_CASE("acknowledge suppresses for the hold-off then re-fires") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    REQUIRE(a.active(1000) == AlarmId::BoxSensorFault);
    a.acknowledge(AlarmId::BoxSensorFault, 1000);     // hold-off to 2800
    CHECK(a.active(1000) == AlarmId::None);
    CHECK(a.any_latched());                        // header marker stays
    tick(a, s, 2799);
    CHECK(a.active(2799) == AlarmId::None);
    tick(a, s, 2801);
    CHECK(a.active(2801) == AlarmId::BoxSensorFault);
}

TEST_CASE("acknowledging one condition does not suppress a different one") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    a.acknowledge(AlarmId::BoxSensorFault, 1000);
    REQUIRE(a.active(1000) == AlarmId::None);

    s.online = false;                              // a DIFFERENT fault appears
    tick(a, s, 1010);
    CHECK(a.active(1010) == AlarmId::ControllerOffline);
}

TEST_CASE("a cleared condition unlatches and drops its acknowledge") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    a.acknowledge(AlarmId::BoxSensorFault, 1000);
    s.sht_fault = 0;
    tick(a, s, 1100);
    CHECK(a.active(1100) == AlarmId::None);
    CHECK_FALSE(a.any_latched());
    // Coming back is a fresh alarm, not one still inside the old hold-off.
    s.sht_fault = 1;
    tick(a, s, 1200);
    CHECK(a.active(1200) == AlarmId::BoxSensorFault);
}

TEST_CASE("a dead cooler we have never had data from still alarms") {
    Alarms a;
    CoolerState s;                 // valid stays false -- no /data ever parsed
    s.availability_seen = true;    // but the retained LWT did arrive
    s.online = false;
    a.update(s, 1000);
    CHECK(a.active(1000) == AlarmId::ControllerOffline);
}

TEST_CASE("silence before any availability message is not an alarm") {
    Alarms a;
    CoolerState s;                 // nothing heard at all yet
    a.update(s, 100000);
    CHECK(a.active(100000) == AlarmId::None);
}

TEST_CASE("offline outranks not-keeping-up") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 12.0f;
    tick(a, s, 1000);
    tick(a, s, 4601);
    REQUIRE(a.active(4601) == AlarmId::NotKeepingUp);
    s.online = false;
    tick(a, s, 4602);
    CHECK(a.active(4602) == AlarmId::ControllerOffline);
}

TEST_CASE("fin sensor fault and no-response fire immediately, each on its own") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.fin_fault = 1;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::FinSensorFault);
    a.acknowledge(AlarmId::FinSensorFault, 1000);
    s.no_response = 1;
    tick(a, s, 1010);
    CHECK(a.active(1010) == AlarmId::NoResponse);
}

TEST_CASE("no-response outranks both sensor faults") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.sht_fault = 1; s.fin_fault = 1; s.no_response = 1;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::NoResponse);
}

TEST_CASE("a null temperature never counts as not keeping up") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.temp = 40.0f; s.temp_valid = false;
    tick(a, s, 1000);
    tick(a, s, 1000 + 7200);
    CHECK(a.active(1000 + 7200) != AlarmId::NotKeepingUp);
}

TEST_CASE("in override, not-keeping-up is measured against the fixed 5 C") {
    Alarms a = make_alarms(); CoolerState s = ok_state();
    s.coolerset = 20;                       // would put the threshold at 27
    std::snprintf(s.mode, sizeof(s.mode), "override");
    s.temp = 11.0f;                         // 5 + 5 = 10 threshold
    tick(a, s, 1000);
    tick(a, s, 4601);
    CHECK(a.active(4601) == AlarmId::NotKeepingUp);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: BUILD
Expected: compile error, `'BoxSensorFault' is not a member of 'AlarmId'`.

- [ ] **Step 3: Implement**

Save this as `/tmp/task3-alarm-h.patch` and apply it from `CoolerPanel/` with `git apply /tmp/task3-alarm-h.patch` (it must apply cleanly; if it does not, the tree is not at the expected state -- stop and check the previous task):

````diff
diff --git a/shared/model/alarm.h b/shared/model/alarm.h
index 2596569..a499c61 100644
--- a/shared/model/alarm.h
+++ b/shared/model/alarm.h
@@ -4,7 +4,8 @@ struct CoolerState;
 
 // Ordered by priority -- lower value wins when several are live at once.
 enum class AlarmId { None = 0, ControllerOffline, ControllerSilent,
-                     SensorFault, NotKeepingUp, COUNT };
+                     NoResponse, BoxSensorFault, FinSensorFault,
+                     NotKeepingUp, COUNT };
 
 // NotKeepingUp's defaults are calibrated against THIS cooler's measured
 // behaviour, not intuition. Its own published hourly history for a normal
````

`shared/model/alarm.cpp`:

```cpp
#include "alarm.h"
#include "cooler_state.h"

void Alarms::update(const CoolerState& s, int64_t now_epoch) {
    // Marks a condition live or clear. `since` records when it first became
    // true, so sustained-duration conditions can measure against it. Clearing
    // also drops any acknowledge, so a condition that comes back is a fresh
    // alarm rather than one still inside an old hold-off.
    auto mark = [&](AlarmId id, bool live) {
        Cond& c = conds_[(int)id];
        if (live && !c.live) c.since = now_epoch;
        if (!live) { c.since = 0; c.ack_until = 0; }
        c.live = live;
    };

    // Gated on availability_seen, NOT valid: the retained LWT can report a
    // dead cooler before /data has ever parsed successfully, and that is
    // exactly the case this alarm must not stay silent for.
    mark(AlarmId::ControllerOffline, s.availability_seen && !s.online);
    mark(AlarmId::ControllerSilent,
         s.valid && (now_epoch - s.last_rx_epoch) > cfg_.silent_s);
    mark(AlarmId::NoResponse, s.valid && s.no_response != 0);
    mark(AlarmId::BoxSensorFault, s.valid && s.sht_fault != 0);
    mark(AlarmId::FinSensorFault, s.valid && s.fin_fault != 0);
    // Measured against the band actually in force: in override the
    // controller holds its fixed 3..5 C, not coolerset +/- range. A null
    // temperature is BoxSensorFault's business, never "not keeping up".
    const float band_hi = cooler_in_override(s) ? 5.0f : (float)(s.coolerset + s.range);
    mark(AlarmId::NotKeepingUp,
         s.valid && s.temp_valid && s.temp > band_hi + (float)cfg_.over_c);
}

AlarmId Alarms::active(int64_t now_epoch) const {
    // Iterates in enum order, which is priority order -- ControllerOffline
    // outranks NotKeepingUp.
    for (int i = 1; i < (int)AlarmId::COUNT; i++) {
        const Cond& c = conds_[i];
        if (!c.live) continue;
        // NotKeepingUp must have held continuously for over_s. The others
        // fire the moment they go true.
        if ((AlarmId)i == AlarmId::NotKeepingUp &&
            (now_epoch - c.since) <= cfg_.over_s) continue;
        // Suppressed only while still inside the hold-off window.
        if (now_epoch < c.ack_until) continue;
        return (AlarmId)i;
    }
    return AlarmId::None;
}

void Alarms::acknowledge(AlarmId id, int64_t now_epoch) {
    if (id == AlarmId::None || id >= AlarmId::COUNT) return;
    conds_[(int)id].ack_until = now_epoch + cfg_.holdoff_s;
}

bool Alarms::any_latched() const {
    for (int i = 1; i < (int)AlarmId::COUNT; i++)
        if (conds_[i].live) return true;
    return false;
}

const char* Alarms::text(AlarmId id) {
    switch (id) {
        case AlarmId::ControllerOffline: return "COOLER OFFLINE";
        case AlarmId::ControllerSilent:  return "NO DATA";
        case AlarmId::NoResponse:        return "AC NOT RESPONDING";
        case AlarmId::BoxSensorFault:    return "BOX SENSOR FAULT";
        case AlarmId::FinSensorFault:    return "COIL SENSOR FAULT";
        case AlarmId::NotKeepingUp:      return "NOT COOLING";
        default:                         return "";
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: BUILD
Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```bash
cd ~/Cooler/CoolerPanel
git add shared/model/alarm.h shared/model/alarm.cpp tests/test_alarm.cpp
git commit -m "feat(alarm): no-response, box/coil sensor faults, override-aware not-keeping-up

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Sim tooling for deterministic screenshots

The sim has no way to show a given controller state without a live broker. The README documents a `--replay` flag that doesn't exist in this codebase. This adds `--fixture FILE` and `--page N`, and makes `--seed-history` produce single-relay data.

**Files:**
- Create: `sim/fixture_feed.cpp`
- Modify: `sim/main.cpp`, `sim/seed_history.cpp`, `shared/ui/nav.h`, `shared/ui/nav.cpp`

**Interfaces:**
- Produces: `void nav_show_page(int index)` (0 Trend, 1 Settings, 2 Detail, no animation) and `bool sim_feed_fixture(const char* path)`.

- [ ] **Step 1: Implement**

`sim/fixture_feed.cpp`:

```cpp
// Simulator-only: route a captured /data payload into the app as if the
// broker had delivered it. Used by --fixture for deterministic screenshots of
// each controller state (tests/fixtures/data_*.json).
#include "app.h"
#include "mqtt_router.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

bool sim_feed_fixture(const char* path) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    const std::string body = ss.str();
    const std::string prefix = router_prefix();
    const std::string avail = prefix + "/availability";
    const std::string data = prefix + "/data";
    app_on_mqtt_message(avail.c_str(), (const uint8_t*)"online", 6);
    app_on_mqtt_message(data.c_str(), (const uint8_t*)body.data(), body.size());
    return true;
}
```

Save this as `/tmp/task4-sim.patch` and apply it from `CoolerPanel/` with `git apply /tmp/task4-sim.patch` (it must apply cleanly; if it does not, the tree is not at the expected state -- stop and check the previous task):

````diff
diff --git a/shared/ui/nav.h b/shared/ui/nav.h
index d6d8d14..4cbbc38 100644
--- a/shared/ui/nav.h
+++ b/shared/ui/nav.h
@@ -2,3 +2,5 @@
 #include "lvgl.h"
 void nav_init(lv_obj_t* root);
 void nav_refresh(void);
+// Jump straight to a page: 0 Trend, 1 Settings, 2 Detail. No animation.
+void nav_show_page(int index);
diff --git a/shared/ui/nav.cpp b/shared/ui/nav.cpp
index adbf639..8a7ef96 100644
--- a/shared/ui/nav.cpp
+++ b/shared/ui/nav.cpp
@@ -49,3 +49,8 @@ void nav_refresh(void) {
     screen_detail_refresh();
     screen_alarm_refresh();      // shows/hides itself based on Alarms::active()
 }
+
+void nav_show_page(int index) {
+    if (!s_tv || index < 0 || index > 2) return;
+    lv_tileview_set_tile_by_index(s_tv, (uint32_t)index, 0, LV_ANIM_OFF);
+}
diff --git a/sim/main.cpp b/sim/main.cpp
index 4a58269..9a4853e 100644
--- a/sim/main.cpp
+++ b/sim/main.cpp
@@ -33,11 +33,16 @@ int main(int argc, char** argv) {
     int frames = 0; const char* shot = nullptr;
     const char* screen = nullptr; // debug flag: --screen boot|setup|setup-qr
     bool seed = false;            // debug flag: --seed-history (see seed_history.cpp)
+    const char* fixture = nullptr; // debug flag: --fixture FILE -- feed one /data
+                                   // payload instead of connecting to the broker
+    int page = -1;                // debug flag: --page 0|1|2 (Trend|Settings|Detail)
     for (int i = 1; i < argc; i++) {
         if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
         else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
         else if (!strcmp(argv[i], "--screen") && i + 1 < argc) screen = argv[++i];
         else if (!strcmp(argv[i], "--seed-history")) seed = true;
+        else if (!strcmp(argv[i], "--fixture") && i + 1 < argc) fixture = argv[++i];
+        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = atoi(argv[++i]);
     }
     g_start = std::chrono::steady_clock::now();
     lv_init();
@@ -115,10 +120,23 @@ int main(int argc, char** argv) {
                 seed_demo_history(5, 10, 80);   // setpoint 5C, maxrun 10m, duty 80%
                 screen_trend_refresh();
             }
-            extern void mqtt_start(const SimConfig&);
-            SimConfig scfg = config_load(argc, argv);
-            mqtt_start(scfg);   // connects in the background; the main loop
-                                 // below already polls mqtt_poll() every tick.
+            if (fixture) {
+                // Deterministic screenshots: route one captured /data (plus an
+                // "online" availability) through the same entry point the
+                // broker would use, and never connect.
+                extern bool sim_feed_fixture(const char* path);
+                if (!sim_feed_fixture(fixture)) {
+                    fprintf(stderr, "cannot read fixture %s\n", fixture);
+                    return 2;
+                }
+            } else {
+                extern void mqtt_start(const SimConfig&);
+                SimConfig scfg = config_load(argc, argv);
+                mqtt_start(scfg);   // connects in the background; the main loop
+                                     // below already polls mqtt_poll() every tick.
+            }
+            if (page >= 0) nav_show_page(page);
+            ui_refresh();
         }
     }
 
diff --git a/sim/seed_history.cpp b/sim/seed_history.cpp
index 8a2f360..83193d8 100644
--- a/sim/seed_history.cpp
+++ b/sim/seed_history.cpp
@@ -2,12 +2,9 @@
 // looked at without waiting for one to happen.
 //
 // The panel builds its history from live /data, which means a fresh run shows
-// an empty canvas and a real unit swap is ten minutes away -- workable for
-// watching the cooler, useless for judging whether the trend reads correctly.
-// This synthesises the same shape the controller actually produces: a 20 min
-// frame (2 x maxrun) split into two windows, each unit running contiguously
-// for its duty share of its own window, temperature sawtoothing against the
-// setpoint band underneath.
+// an empty canvas. This synthesises the shape the v4 controller produces: the
+// single relay cycling on a maxrun-long frame at the given duty, temperature
+// sawtoothing against the setpoint band underneath.
 //
 // Debug affordance, never compiled into the device firmware -- and it seeds
 // the panel's own buffer only, so nothing is published anywhere.
@@ -23,28 +20,19 @@ void seed_demo_history(int coolerset, int maxrun_min, int dutypercent) {
     const int step = 30;                             // /data's sampling period
     const int64_t now = platform_epoch_utc();
     const int64_t span = 7 * 24 * 3600;
-    const int frame = 2 * maxrun_min * 60;           // both windows
-    const int window = frame / 2;                    // one unit's window
-    const int on_s = window * dutypercent / 100;     // its contiguous run
+    const int frame = maxrun_min * 60;               // one cooling cycle
+    const int on_s = frame * dutypercent / 100;      // relay closed this long
 
     // A box being held steady neither warms nor cools on average, so the two
-    // rates have to balance at THIS duty or the trace walks off the axis: the
-    // frame spends on_s cooling against (window - on_s) drifting back, so the
-    // warm rate is that ratio times the cool rate. Getting this wrong is not
-    // subtle -- an 0.055/0.11 pair drifts -0.88 C per frame, which is -440 C
-    // by the end of the seeded week.
+    // rates have to balance at THIS duty or the trace walks off the axis.
     const float cool = 0.055f;
-    const float warm = cool * (float)on_s / (float)(window - on_s);
+    const float warm = cool * (float)on_s / (float)(frame - on_s);
 
     float t = (float)coolerset + 0.6f;
     for (int64_t age = span; age > 0; age -= step) {
         const int64_t at = now - age;
         const int phase = (int)(at % frame);
-
-        // Which unit owns this instant, and is it inside its run?
-        uint8_t ac = 0;
-        if (phase < window)  ac = (phase < on_s) ? 1 : 0;
-        else                 ac = (phase - window < on_s) ? 2 : 0;
+        const uint8_t ac = (phase < on_s) ? 1 : 0;   // relay closed
 
         t += ac ? -cool : warm;
         // Slow diurnal wander on top, so the auto-ranged axis has something
````

- [ ] **Step 2: Build and screenshot**

Run: BUILD, then `SHOT --seed-history --fixture tests/fixtures/data_normal.json --frames 1100 --screenshot /tmp/t4.png`
Expected: prints `wrote /tmp/t4.png`, with no MQTT connection attempt in the output. The image shows the *old* two-unit Trend layout with the big temperature reading `4.8` and a week of cyan/grey sawtooth bars. The old cards show `Off` and `No`, because the legacy fields are zero. Task 5 replaces them.

- [ ] **Step 3: Commit**

```bash
cd ~/Cooler/CoolerPanel
git add sim/fixture_feed.cpp sim/main.cpp sim/seed_history.cpp shared/ui/nav.h shared/ui/nav.cpp
git commit -m "feat(sim): --fixture and --page for deterministic screenshots; single-relay seed

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Status text and the single-unit Trend screen

The Trend screen keeps the Quiet Slate grammar: only the thing currently happening is coloured. Row 1 is temperature, then humidity over **COIL** (fin temperature, amber while defrosting). Row 2 is a wide **status** card (mode in the key; `Cooling 2m` / `Defrost 1m` / `Waiting 3m` / `Resting 6m` / `Idle 15m`) and an **AC UNIT** card (`Running` / `Starting` / `Stopped` / `--`). In override, the setpoint line becomes an amber `OVERRIDE 3-5  SWITCH` and the chart band shades 3–5 °C.

**Files:**
- Create: `shared/model/status_text.h`, `shared/model/status_text.cpp`, `tests/test_status_text.cpp`
- Modify: `CMakeLists.txt` (register the test)
- Rewrite: `shared/ui/screen_trend.cpp`

**Interfaces:**
- Produces: `fmt_dur(uint32_t, char*, size_t)`, `fmt_mode_label(const CoolerState&)`, `fmt_state_value(...)`, `fmt_state_key(...)`, `fmt_compressor(const CoolerState&)`, `fmt_fincal(...)`. Exact strings are in the tests.

- [ ] **Step 1: Write the failing tests and register them**

`tests/test_status_text.cpp`:

```cpp
#include <doctest/doctest.h>
#include "status_text.h"
#include "cooler_state.h"
#include <cstdio>
#include <string>

static CoolerState st(const char* mode, const char* state) {
    CoolerState s;
    s.valid = true;
    std::snprintf(s.mode, sizeof(s.mode), "%s", mode);
    std::snprintf(s.state, sizeof(s.state), "%s", state);
    return s;
}

TEST_CASE("fmt_dur picks seconds, minutes, or hours") {
    char b[16];
    CHECK(std::string(fmt_dur(0, b, sizeof(b))) == "0s");
    CHECK(std::string(fmt_dur(59, b, sizeof(b))) == "59s");
    CHECK(std::string(fmt_dur(60, b, sizeof(b))) == "1m");
    CHECK(std::string(fmt_dur(3599, b, sizeof(b))) == "59m");
    CHECK(std::string(fmt_dur(3900, b, sizeof(b))) == "1h 05m");
}

TEST_CASE("mode labels") {
    CHECK(std::string(fmt_mode_label(st("normal", "idle"))) == "NORMAL");
    CHECK(std::string(fmt_mode_label(st("override", "idle"))) == "OVERRIDE");
    CHECK(std::string(fmt_mode_label(st("fin-proxy", "idle"))) == "FIN PROXY");
    CHECK(std::string(fmt_mode_label(st("override-proxy", "idle"))) == "OVR + FIN PROXY");
    CHECK(std::string(fmt_mode_label(st("blind", "idle"))) == "BLIND TIMER");
    CHECK(std::string(fmt_mode_label(CoolerState{})) == "--");
}

TEST_CASE("state value uses the timer that matters for each state") {
    char b[32];
    CoolerState s = st("normal", "cooling"); s.run_s = 142;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Cooling 2m");
    s = st("normal", "defrost"); s.off_s = 75;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Defrost 1m");
    s = st("normal", "wait"); s.hold_s = 200;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Waiting 3m");
    s = st("fin-proxy", "rest"); s.hold_s = 360;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Resting 6m");
    s = st("fin-proxy", "rest"); s.hold_s = 0;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Resting");
    s = st("normal", "idle"); s.off_s = 900;
    CHECK(std::string(fmt_state_value(s, b, sizeof(b))) == "Idle 15m");
    CHECK(std::string(fmt_state_value(CoolerState{}, b, sizeof(b))) == "--");
}

TEST_CASE("state key adds the min-run countdown only while it holds the relay") {
    char b[48];
    CoolerState s = st("normal", "cooling"); s.hold_s = 38;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "NORMAL - MIN-RUN 38s");
    s.hold_s = 0;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "NORMAL");
    s = st("override", "wait"); s.hold_s = 100;
    CHECK(std::string(fmt_state_key(s, b, sizeof(b))) == "OVERRIDE");
}

TEST_CASE("compressor wording") {
    CoolerState s = st("normal", "cooling");
    s.compressor = 1;               CHECK(std::string(fmt_compressor(s)) == "Running");
    s.compressor = 0; s.relay = 1;  CHECK(std::string(fmt_compressor(s)) == "Starting");
    s.relay = 0;                    CHECK(std::string(fmt_compressor(s)) == "Stopped");
    s.compressor = -1;              CHECK(std::string(fmt_compressor(s)) == "--");
}

TEST_CASE("fin calibration summary") {
    char b[40];
    CoolerState s = st("normal", "idle");
    CHECK(std::string(fmt_fincal(s, b, sizeof(b))) == "fin uncalibrated");
    s.fin_cal = 1;
    CHECK(std::string(fmt_fincal(s, b, sizeof(b))) == "fin cal ok");
    s.cal_active = 1; s.cal_points = 3; s.cal_span = 6.2f;
    CHECK(std::string(fmt_fincal(s, b, sizeof(b))) == "calibrating 3 pts 6.2C");
}
```

Save this as `/tmp/task5-cmake.patch` and apply it from `CoolerPanel/` with `git apply /tmp/task5-cmake.patch` (it must apply cleanly; if it does not, the tree is not at the expected state -- stop and check the previous task):

````diff
diff --git a/CMakeLists.txt b/CMakeLists.txt
index 2fb30fb..6d8eae7 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -40,6 +40,7 @@ add_executable(cooler_tests
     tests/test_commands.cpp
     tests/test_alarm.cpp
     tests/test_panel_config.cpp
+    tests/test_status_text.cpp
     config/tests/test_device_config.cpp)
 target_include_directories(cooler_tests PRIVATE tests)
 target_link_libraries(cooler_tests PRIVATE cooler_shared cooler_config lvgl doctest::doctest)
````

- [ ] **Step 2: Run to verify it fails**

Run: BUILD
Expected: compile error, `status_text.h: No such file or directory`.

- [ ] **Step 3: Implement the text module**

`shared/model/status_text.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
struct CoolerState;

// Plain-text renderings of the controller's run state, shared by the Trend
// and Detail screens. Pure functions (no LVGL) so they are unit-tested.

// "45s" / "12m" / "1h 05m".
const char* fmt_dur(uint32_t secs, char* out, size_t cap);

// Human mode label: "NORMAL", "OVERRIDE", "FIN PROXY", "OVR + FIN PROXY",
// "BLIND TIMER"; "--" before any /data.
const char* fmt_mode_label(const CoolerState& s);

// The status card's big line: "Cooling 2m", "Defrost 1m", "Waiting 3m",
// "Resting 6m", "Idle 15m"; "--" before any /data.
const char* fmt_state_value(const CoolerState& s, char* out, size_t cap);

// The status card's key line: the mode label, plus the minimum-run countdown
// while it is what is holding the relay closed ("NORMAL - MIN-RUN 38s").
const char* fmt_state_key(const CoolerState& s, char* out, size_t cap);

// Compressor card: "Running", "Starting" (relay closed, compressor not yet
// seen), "Stopped", or "--" when the fin sensor cannot tell.
const char* fmt_compressor(const CoolerState& s);

// Fin calibration summary: "fin cal ok", "fin uncalibrated",
// "calibrating 3 pts 6.2C".
const char* fmt_fincal(const CoolerState& s, char* out, size_t cap);
```

`shared/model/status_text.cpp`:

```cpp
#include "status_text.h"
#include "cooler_state.h"
#include <cstdio>
#include <cstring>

const char* fmt_dur(uint32_t secs, char* out, size_t cap) {
    if (secs < 60)        std::snprintf(out, cap, "%us", (unsigned)secs);
    else if (secs < 3600) std::snprintf(out, cap, "%um", (unsigned)(secs / 60));
    else                  std::snprintf(out, cap, "%uh %02um", (unsigned)(secs / 3600),
                                        (unsigned)((secs % 3600) / 60));
    return out;
}

static bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

const char* fmt_mode_label(const CoolerState& s) {
    if (!s.valid || !s.mode[0])       return "--";
    if (is(s.mode, "normal"))         return "NORMAL";
    if (is(s.mode, "override"))       return "OVERRIDE";
    if (is(s.mode, "fin-proxy"))      return "FIN PROXY";
    if (is(s.mode, "override-proxy")) return "OVR + FIN PROXY";
    if (is(s.mode, "blind"))          return "BLIND TIMER";
    return s.mode;
}

const char* fmt_state_value(const CoolerState& s, char* out, size_t cap) {
    if (!s.valid || !s.state[0]) { std::snprintf(out, cap, "--"); return out; }
    char d[16];
    if (is(s.state, "cooling"))      std::snprintf(out, cap, "Cooling %s", fmt_dur(s.run_s, d, sizeof(d)));
    else if (is(s.state, "defrost")) std::snprintf(out, cap, "Defrost %s", fmt_dur(s.off_s, d, sizeof(d)));
    else if (is(s.state, "wait"))    std::snprintf(out, cap, "Waiting %s", fmt_dur(s.hold_s, d, sizeof(d)));
    else if (is(s.state, "rest"))    {
        if (s.hold_s) std::snprintf(out, cap, "Resting %s", fmt_dur(s.hold_s, d, sizeof(d)));
        else          std::snprintf(out, cap, "Resting");
    }
    else if (is(s.state, "idle"))    std::snprintf(out, cap, "Idle %s", fmt_dur(s.off_s, d, sizeof(d)));
    else                             std::snprintf(out, cap, "%s", s.state);
    return out;
}

const char* fmt_state_key(const CoolerState& s, char* out, size_t cap) {
    const char* m = fmt_mode_label(s);
    if (s.valid && is(s.state, "cooling") && s.hold_s > 0) {
        char d[16];
        std::snprintf(out, cap, "%s - MIN-RUN %s", m, fmt_dur(s.hold_s, d, sizeof(d)));
    } else {
        std::snprintf(out, cap, "%s", m);
    }
    return out;
}

const char* fmt_compressor(const CoolerState& s) {
    if (!s.valid || s.compressor < 0) return "--";
    if (s.compressor == 1) return "Running";
    return s.relay ? "Starting" : "Stopped";
}

const char* fmt_fincal(const CoolerState& s, char* out, size_t cap) {
    if (s.cal_active)   std::snprintf(out, cap, "calibrating %d pts %.1fC", s.cal_points, (double)s.cal_span);
    else if (s.fin_cal) std::snprintf(out, cap, "fin cal ok");
    else                std::snprintf(out, cap, "fin uncalibrated");
    return out;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: BUILD
Expected: `100% tests passed`.

- [ ] **Step 5: Rewrite the Trend screen**

`shared/ui/screen_trend.cpp`:

```cpp
#include "screen_trend.h"
#include "chart.h"
#include "history.h"
#include "cooler_state.h"
#include "app.h"
#include "theme.h"
#include "icons.h"
#include "panel_config.h"
#include "status_text.h"
#include "platform.h"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// "Quiet Slate": a calm domestic display, not an instrument panel.
//
// The governing rule is that ONLY THE THING CURRENTLY HAPPENING IS COLOURED.
// Everything at rest stays in the greys. The previous layout coloured six
// cards at once, which is visually loud and, worse, makes the one card you
// need to notice indistinguishable from the five you don't.
//
// Asymmetric top: temperature is the reason you look at this panel, so it
// gets two thirds of the row; humidity and the coil (fin) temperature stack
// beside it -- the two readings that explain the box temperature.
//
// The second row is the one AC unit: what the controller is doing (the wide
// status card, which also names the mode) and what the compressor is actually
// doing (inferred from the fin slope). Keeping those two side by side is the
// point: "Cooling" beside "Starting" for long is the AC not answering.
//
// Everything sits on a three-column grid: 3 * 141 + 2 * 12 gutters + 2 * 16
// margins == 480, and the temperature block spans the first two columns.
//
//   8..30     header (name / age)
//   38..150   temp (cols 1-2) + humidity over coil (col 3)
//   158..204  status (cols 1-2) | compressor
//   214..382  graph
//   390..422  zoom
//   430..450  footer
// ---------------------------------------------------------------------------
#define PAD        16
#define GUT        12
#define COL_W      141
#define COL2_X     (PAD + COL_W + GUT)
#define COL3_X     (PAD + (COL_W + GUT) * 2)
#define HDR_Y      8
#define R1_Y       38
#define R1_H       112
#define BIG_W      (COL_W * 2 + GUT)
#define HUM_H      58
#define R2_Y       158
#define R2_H       46
#define COIL_Y     (R1_Y + HUM_H + 8)

#define GRAPH_X    36
#define GRAPH_Y    214
#define GRAPH_W    408
#define GRAPH_H    168
#define NCOLS      102

#define ZOOM_Y     390
#define FOOT_Y     430

static lv_obj_t* s_root;
static lv_obj_t* s_canvas;
static lv_obj_t* s_foot_lbl;
static lv_obj_t* s_age_lbl;
static lv_color_t* s_cbuf;
static int s_zoom = 3600;
static lv_obj_t* s_ax_t[3];
static lv_obj_t* s_ax_h[3];

// big temperature block
static lv_obj_t* s_big_box; static lv_obj_t* s_big_n; static lv_obj_t* s_big_u; static lv_obj_t* s_big_s;
// side stack + pills: a panel with a small key, a value, and an optional icon
struct Cell { lv_obj_t* box; lv_obj_t* k; lv_obj_t* v; lv_obj_t* ic; };
static Cell s_hum, s_coil, s_state, s_comp;

void trend_set_zoom(int seconds) { s_zoom = seconds; screen_trend_refresh(); }
int  trend_zoom(void) { return s_zoom; }

static void panel_style(lv_obj_t* o, int radius) {
    lv_obj_set_style_bg_color(o, th_surface(), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void cell_init(Cell& c, lv_obj_t* parent, int x, int y, int w, int h,
                      const char* key, int radius) {
    c.box = lv_obj_create(parent);
    lv_obj_set_size(c.box, w, h);
    lv_obj_set_pos(c.box, x, y);
    panel_style(c.box, radius);
    c.k = lv_label_create(c.box);
    lv_obj_set_pos(c.k, 14, 9);
    lv_obj_set_style_text_color(c.k, th_muted(), 0);
    lv_label_set_text(c.k, key);
    c.v = lv_label_create(c.box);
    lv_obj_set_pos(c.v, 14, 24);
    lv_obj_set_style_text_font(c.v, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(c.v, th_ink(), 0);
    lv_label_set_text(c.v, "--");
    c.ic = nullptr;
}

// The three compact cells (coil, status, compressor) share this tighter
// stack, so the side-column coil card and the row-two cards read as the same
// kind of card.
static void cell_compact(Cell& c) {
    lv_obj_set_pos(c.k, 14, 6);
    lv_obj_set_pos(c.v, 14, 18);
    lv_obj_set_style_text_font(c.v, &lv_font_montserrat_20, 0);
}

// Icons sit right-of-centre. The margin is 8, not the 12 used elsewhere,
// because the longest value ("Running" at montserrat_20) already reaches
// x=103 of the 141px card; 12 would put the icon's left edge exactly on the
// tail of the "g".
static void cell_icon(Cell& c, const lv_image_dsc_t* d) {
    c.ic = lv_image_create(c.box);
    lv_image_set_src(c.ic, d);
    lv_obj_align(c.ic, LV_ALIGN_RIGHT_MID, -8, 0);
    // A8 source: the recolor style IS the fill, so this is not a tint over
    // existing colour -- leave it unset and the icon draws black on black.
    lv_obj_set_style_image_recolor_opa(c.ic, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(c.ic, th_muted(), 0);
}

// Live cells get a tinted fill and a coloured value; at-rest cells stay grey.
// This is the whole visual grammar of the screen in one function. The accent
// trio is a parameter because defrost wears the warning amber, not the
// cooling cyan.
static void cell_set(Cell& c, const char* text, bool live, lv_color_t accent,
                     lv_color_t fill = th_cool_bg(), lv_color_t dim = th_cool_dim()) {
    lv_label_set_text(c.v, text);
    lv_obj_set_style_bg_color(c.box, live ? fill : th_surface(), 0);
    lv_obj_set_style_text_color(c.v, live ? accent : th_ink2(), 0);
    lv_obj_set_style_text_color(c.k, live ? dim : th_muted(), 0);
    if (c.ic) lv_obj_set_style_image_recolor(c.ic, live ? accent : th_muted(), 0);
}

lv_obj_t* screen_trend_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_bg_color(s_root, th_page(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name = lv_label_create(s_root);
    lv_obj_set_pos(name, PAD, HDR_Y);
    lv_obj_set_style_text_color(name, th_ink2(), 0);
    lv_label_set_text(name, "COOLERMON");

    s_age_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_age_lbl, 420, HDR_Y);
    lv_obj_set_style_text_color(s_age_lbl, th_muted(), 0);
    lv_label_set_text(s_age_lbl, "--");

    // Temperature: the one figure worth crossing a room for.
    s_big_box = lv_obj_create(s_root);
    lv_obj_set_size(s_big_box, BIG_W, R1_H);
    lv_obj_set_pos(s_big_box, PAD, R1_Y);
    panel_style(s_big_box, 20);
    s_big_n = lv_label_create(s_big_box);
    lv_obj_set_pos(s_big_n, 18, 14);
    lv_obj_set_style_text_font(s_big_n, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_big_n, th_ink(), 0);
    s_big_u = lv_label_create(s_big_box);
    lv_obj_set_pos(s_big_u, 168, 30);
    lv_obj_set_style_text_font(s_big_u, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_big_u, th_muted(), 0);
    lv_label_set_text(s_big_u, "C");
    s_big_s = lv_label_create(s_big_box);
    lv_obj_set_pos(s_big_s, 20, 80);
    lv_obj_set_style_text_color(s_big_s, th_muted(), 0);

    cell_init(s_hum,   s_root, COL3_X, R1_Y,   COL_W, HUM_H, "HUMIDITY",   18);
    cell_init(s_coil,  s_root, COL3_X, COIL_Y, COL_W, R2_H,  "COIL",       16);
    cell_init(s_state, s_root, PAD,    R2_Y,   BIG_W, R2_H,  "--",         16);
    cell_init(s_comp,  s_root, COL3_X, R2_Y,   COL_W, R2_H,  "AC UNIT",    16);

    for (Cell* c : {&s_coil, &s_state, &s_comp}) cell_compact(*c);
    cell_icon(s_coil,  &icon_snowflake);  // the evaporator: ices, defrosts
    cell_icon(s_state, &icon_duty);       // the controller's cycle
    cell_icon(s_comp,  &icon_aircon);     // the unit itself

    s_cbuf = (lv_color_t*)lv_malloc(
        LV_CANVAS_BUF_SIZE(GRAPH_W, GRAPH_H, 16, LV_DRAW_BUF_STRIDE_ALIGN));
    if (s_cbuf) {
        s_canvas = lv_canvas_create(s_root);
        lv_canvas_set_buffer(s_canvas, s_cbuf, GRAPH_W, GRAPH_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_canvas, GRAPH_X, GRAPH_Y);
    }

    // Axis ticks, coloured to their series so no legend is needed. The left
    // (temperature) scale is neutral, not cyan: cyan means "cooling was
    // requested", and an axis is not a series.
    for (int i = 0; i < 3; i++) {
        const int y = GRAPH_Y - 7 + i * ((GRAPH_H - 1) / 2);
        s_ax_t[i] = lv_label_create(s_root);
        lv_obj_set_pos(s_ax_t[i], 4, y);
        lv_obj_set_style_text_color(s_ax_t[i], th_ink2(), 0);
        lv_label_set_text(s_ax_t[i], "");
        s_ax_h[i] = lv_label_create(s_root);
        lv_obj_set_pos(s_ax_h[i], GRAPH_X + GRAPH_W + 6, y);
        lv_obj_set_style_text_color(s_ax_h[i], th_rh(), 0);
        lv_label_set_text(s_ax_h[i], "");
    }

    static const char* zooms[] = {"1h", "24h", "7d"};
    static const int zsec[] = {3600, 86400, 604800};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = lv_button_create(s_root);
        lv_obj_set_size(b, 100, 32);
        lv_obj_set_pos(b, 60 + i * 120, ZOOM_Y);
        lv_obj_set_style_bg_color(b, th_surface(), 0);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, zooms[i]);
        lv_obj_set_style_text_color(l, th_ink2(), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            trend_set_zoom((int)(intptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, (void*)(intptr_t)zsec[i]);
    }

    s_foot_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_foot_lbl, PAD, FOOT_Y);
    lv_obj_set_style_text_color(s_foot_lbl, th_muted(), 0);

    screen_trend_refresh();
    return s_root;
}

static void axis_clear(void) {
    for (int i = 0; i < 3; i++) { lv_label_set_text(s_ax_t[i], ""); lv_label_set_text(s_ax_h[i], ""); }
}

// "12s" / "4m" / "2h". Terse: it shares the header row.
static void fmt_age(char* out, size_t n, int64_t secs) {
    if (secs < 0)         snprintf(out, n, "--");
    else if (secs < 60)   snprintf(out, n, "%ds", (int)secs);
    else if (secs < 3600) snprintf(out, n, "%dm", (int)(secs / 60));
    else                  snprintf(out, n, "%dh", (int)(secs / 3600));
}

static void draw_graph(void) {
    if (!s_canvas) return;
    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);
    lv_canvas_fill_bg(s_canvas, th_surface(), LV_OPA_COVER);

    const CoolerState& st = cooler_state();
    History& h = panel_history();
    if (h.size() < 2) { axis_clear(); lv_canvas_finish_layer(s_canvas, &layer); return; }

    const int64_t to = h.newest_epoch() + 1;
    const int64_t from = to - s_zoom;
    static Column cols[NCOLS];
    chart_downsample(h, from, to, cols, NCOLS);

    // Auto-range on the data, always including the setpoint band.
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < NCOLS; i++) {
        if (!cols[i].has) continue;
        if (cols[i].tmin < lo) lo = cols[i].tmin;
        if (cols[i].tmax > hi) hi = cols[i].tmax;
    }
    if (lo > hi) { axis_clear(); lv_canvas_finish_layer(s_canvas, &layer); return; }
    // The band actually in force: override runs the controller's fixed
    // 3..5 C thermostat, whatever coolerset/range say.
    const bool ovr = cooler_in_override(st);
    const float band_lo = ovr ? 3.0f : (float)(st.coolerset - st.range);
    const float band_hi = ovr ? 5.0f : (float)(st.coolerset + st.range);
    if (band_lo < lo) lo = band_lo;
    if (band_hi > hi) hi = band_hi;
    if (hi - lo < 1.0f) { hi = lo + 1.0f; }
    const float pad = (hi - lo) * 0.1f;
    lo -= pad; hi += pad;

    auto y_of = [&](float v) -> int32_t {
        return (int32_t)(GRAPH_H - 1 - ((v - lo) / (hi - lo)) * (GRAPH_H - 1));
    };

    // Humidity gets its OWN vertical range. Temp sits near 14 and RH near 84,
    // so a shared axis would squash the temperature trace into a flat line
    // along the bottom and tell you nothing. Independent scales mean the two
    // traces are each readable; the legend says which is which, and neither
    // trace's absolute height is comparable to the other's -- only its shape.
    float hlo = 1e9f, hhi = -1e9f;
    for (int i = 0; i < NCOLS; i++) {
        if (!cols[i].has) continue;
        if (cols[i].hmin < hlo) hlo = cols[i].hmin;
        if (cols[i].hmax > hhi) hhi = cols[i].hmax;
    }
    const bool have_rh = (hlo <= hhi);
    if (have_rh) {
        if (hhi - hlo < 2.0f) { hhi = hlo + 2.0f; }
        const float hpad = (hhi - hlo) * 0.1f;
        hlo -= hpad; hhi += hpad;
    }
    auto hy_of = [&](float v) -> int32_t {
        return (int32_t)(GRAPH_H - 1 - ((v - hlo) / (hhi - hlo)) * (GRAPH_H - 1));
    };

    // Tick values, top to bottom, now that both ranges are settled.
    char tk[16];
    for (int i = 0; i < 3; i++) {
        snprintf(tk, sizeof(tk), "%.0f", (double)(hi - (hi - lo) * i / 2.0f));
        lv_label_set_text(s_ax_t[i], tk);
        if (have_rh) {
            snprintf(tk, sizeof(tk), "%.0f", (double)(hhi - (hhi - hlo) * i / 2.0f));
            lv_label_set_text(s_ax_h[i], tk);
        } else {
            lv_label_set_text(s_ax_h[i], "");
        }
    }

    // Setpoint band
    lv_draw_rect_dsc_t band;
    lv_draw_rect_dsc_init(&band);
    band.bg_color = lv_color_hex(0x212a26);
    band.bg_opa = LV_OPA_60;
    lv_area_t ba = {0, y_of(band_hi), GRAPH_W - 1, y_of(band_lo)};
    lv_draw_rect(&layer, &band, &ba);

    // Temperature min/max bars, cyan where the controller was requesting
    // cooling for most of the column -- the same cyan the status card wears
    // while cooling, so the card is the legend.
    //
    // Idle stretches are neutral grey rather than a dimmed accent: on a graph
    // the shape of the curve is the data, and it has to stay legible whether
    // or not anything was cooling. th_ink2() sits just under both accents in
    // luminance, so running periods still read as the brighter, saturated
    // ones without idle periods fading out.
    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.bg_opa = LV_OPA_COVER;
    const int32_t colw = GRAPH_W / NCOLS;
    for (int i = 0; i < NCOLS; i++) {
        if (!cols[i].has) continue;      // gap stays blank
        bar.bg_color = cols[i].ac ? th_cool() : th_ink2();
        int32_t y0 = y_of(cols[i].tmax);
        int32_t y1 = y_of(cols[i].tmin);
        if (y1 - y0 < 2) y1 = y0 + 2;    // keep flat runs visible
        lv_area_t a = {i * colw, y0, i * colw + colw - 1, y1};
        lv_draw_rect(&layer, &bar, &a);
    }

    // Humidity: a thin trace rather than filled bars, so it reads as an
    // overlay on the temperature rather than competing with it.
    if (have_rh) {
        lv_draw_rect_dsc_t rh;
        lv_draw_rect_dsc_init(&rh);
        rh.bg_color = th_rh();
        rh.bg_opa = LV_OPA_COVER;
        for (int i = 0; i < NCOLS; i++) {
            if (!cols[i].has) continue;    // same honest gap as the temp trace
            const float mid = (cols[i].hmin + cols[i].hmax) * 0.5f;
            int32_t y = hy_of(mid);
            if (y < 0) y = 0;
            if (y > GRAPH_H - 3) y = GRAPH_H - 3;
            lv_area_t a = {i * colw, y, i * colw + colw - 1, y + 2};
            lv_draw_rect(&layer, &rh, &a);
        }
    }
    lv_canvas_finish_layer(s_canvas, &layer);
}

void screen_trend_refresh(void) {
    if (!s_root) return;
    const CoolerState& st = cooler_state();
    const PanelConfig& pc = panel_config();
    char buf[64], age[24];

    const int64_t secs = st.valid ? (platform_epoch_utc() - st.last_rx_epoch) : -1;
    fmt_age(age, sizeof(age), secs);
    lv_label_set_text(s_age_lbl, age);
    // Stale data is the one non-alarm thing that must still catch the eye.
    lv_obj_set_style_text_color(s_age_lbl,
        (secs < 0 || secs > 90) ? th_warn() : th_muted(), 0);

    if (st.valid && st.temp_valid) snprintf(buf, sizeof(buf), "%.1f", (double)st.temp);
    else                           snprintf(buf, sizeof(buf), "--.-");
    lv_label_set_text(s_big_n, buf);
    // In override the panel's setpoint is NOT in force -- say so where the
    // setpoint normally is, in the warning colour, and name the cause.
    if (st.valid && cooler_in_override(st)) {
        char src[8];
        snprintf(src, sizeof(src), "%s", st.override_src);
        for (char* c = src; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        snprintf(buf, sizeof(buf), "OVERRIDE 3-5  %s", src);
        lv_obj_set_style_text_color(s_big_s, th_warn(), 0);
    } else {
        snprintf(buf, sizeof(buf), "SET %d  BAND +-%d", st.coolerset, st.range);
        lv_obj_set_style_text_color(s_big_s, th_muted(), 0);
    }
    lv_label_set_text(s_big_s, buf);

    // Humidity: the band from panel config still governs, but as a text
    // colour rather than a filled card -- a red block for "a bit humid" would
    // shout as loudly as a genuine alarm, which is exactly what this
    // direction is trying to stop doing.
    if (st.valid && st.humidity_valid) {
        const int rh = (int)(st.humidity + 0.5f);
        snprintf(buf, sizeof(buf), "%d%%", rh);
        lv_label_set_text(s_hum.v, buf);
        lv_obj_set_style_text_color(s_hum.v,
            rh < pc.hum_low ? th_warn() : rh > pc.hum_high ? th_crit() : th_ink(), 0);
    } else {
        lv_label_set_text(s_hum.v, "--%");
        lv_obj_set_style_text_color(s_hum.v, th_ink2(), 0);
    }

    // Coil: only coloured while it is defrosting -- that is the one moment
    // the fin reading is the most important number on the screen.
    const bool defrost = st.valid && st.defrost;
    if (st.valid && st.fin_temp_valid) snprintf(buf, sizeof(buf), "%.1f", (double)st.fin_temp);
    else                               snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(s_coil.k, defrost ? "COIL  ICE" : "COIL");
    cell_set(s_coil, buf, defrost, th_warn(), th_duty(), th_muted());

    // Status: what the controller is doing and, in the key, which mode.
    char key[48];
    fmt_state_key(st, key, sizeof(key));
    lv_label_set_text(s_state.k, key);
    fmt_state_value(st, buf, sizeof(buf));
    const bool cooling = st.valid && st.relay;
    if (defrost) cell_set(s_state, buf, true, th_warn(), th_duty(), th_muted());
    else         cell_set(s_state, buf, cooling, th_cool());

    // Compressor: what the AC is actually doing, per the fin slope.
    cell_set(s_comp, fmt_compressor(st), st.valid && st.compressor == 1, th_cool());

    char cal[40], d1[16], d2[16];
    fmt_fincal(st, cal, sizeof(cal));
    snprintf(buf, sizeof(buf), "min-off %s   min-run %s   %s",
             fmt_dur((uint32_t)st.minofftime * 60u, d1, sizeof(d1)),
             fmt_dur((uint32_t)st.minruntime, d2, sizeof(d2)), cal);
    lv_label_set_text(s_foot_lbl, buf);

    draw_graph();
}
```

- [ ] **Step 6: Build and check every state**

Run: BUILD, then for each `<f>` in `normal defrost override`: `SHOT --seed-history --fixture tests/fixtures/data_<f>.json --frames 1100 --screenshot /tmp/t5_<f>.png`

Expected:
- **normal:** `4.8` with `SET 4  BAND +-2`. Humidity `78%`. COIL `1.6` grey. The status card is cyan, key `NORMAL - MIN-RUN 38s`, value `Cooling 2m`. AC UNIT is cyan `Running`. The footer reads `min-off 5m   min-run 3m   fin cal ok`.
- **defrost:** the COIL card is amber, key `COIL  ICE`, value `-0.4`. The status card is amber `Defrost 1m`. AC UNIT is grey `Stopped`.
- **override:** `3.4`, then `OVERRIDE 3-5  SWITCH` in amber. Status `OVERRIDE` / `Idle 15m`. The left axis spans about 3–6.

No card label may overlap its icon.

- [ ] **Step 7: Commit**

```bash
cd ~/Cooler/CoolerPanel
git add shared/model/status_text.h shared/model/status_text.cpp tests/test_status_text.cpp CMakeLists.txt shared/ui/screen_trend.cpp
git commit -m "feat(trend): single-unit status, coil and AC cards; override-aware band

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Tabbed Settings screen

**Files:**
- Rewrite: `shared/ui/screen_settings.cpp`, `shared/ui/screen_settings.h`
- Modify: `sim/main.cpp` (`--tab`)

**Interfaces:**
- Consumes: `Bound::group`, `Commands::request/display_value/is_pending/take_toast`, `cooler_in_override`.
- Produces: `void screen_settings_show_tab(int tab)` (0 Box, 1 Coil, 2 Timing).
- Behaviour change from v3: settings stay editable in every mode (v2 always publishes stored values). They're only read-only before the first `/data`. The override toggle is gone.

- [ ] **Step 1: Implement**

`shared/ui/screen_settings.h`:

```cpp
#pragma once
#include "lvgl.h"
lv_obj_t* screen_settings_create(lv_obj_t* parent);
void screen_settings_refresh(void);
// Select a tab: 0 Box, 1 Coil, 2 Timing. Used by the sim's --tab flag.
void screen_settings_show_tab(int tab);
```

`shared/ui/screen_settings.cpp`:

```cpp
#include "screen_settings.h"
#include "bounds.h"
#include "commands.h"
#include "cooler_state.h"
#include "app.h"
#include "platform.h"
#include "theme.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// Ten settable values do not fit one fixed 480 px page at a finger-sized row
// height, and every screen here deliberately refuses to scroll. So the page
// is split into three tabs by what the values are ABOUT (bounds.h Group):
//
//   Box    -- the target: set point, band, history spacing
//   Coil   -- icing protection: cutoff, clear, settle
//   Timing -- compressor protection and the sensor-failure fallback
//
// Unlike v3, the v4 controller always publishes its stored settings (never
// mode-dependent effective values), so nothing here is read-only in override.
// Override's fixed 3-5 C thermostat simply ignores coolerset/range, and the
// banner says so.
#define BANNER_Y   6
#define TAB_Y      36
#define TAB_H      40
#define ROW0_Y     88
#define ROW_H      56
#define CHIP_ROW_H 40
#define TOAST_MS   4000   // how long a take_toast() message stays on screen
#define MAX_ROWS   16

static const int  kSampleVals[]        = {10, 60, 300, 900, 3600};
static const char* const kSampleLbls[] = {"10s", "1m", "5m", "15m", "1h"};
static const int  kSampleN = 5;

static const char* const kTabNames[] = {"Box", "Coil", "Timing"};
static const char* const kTabHints[] = {
    "Set point and band apply in normal mode.\nOverride holds a fixed 3-5 C.",
    "Cooling stops when the coil reaches Ice cutoff\nand resumes once it is above Ice clear.",
    "Set Min off / Min run from the AC timing test.\nMax run and Backup duty only apply if a sensor fails.",
};

static lv_obj_t* s_root;
static lv_obj_t* s_banner;
static lv_obj_t* s_toast;
static lv_obj_t* s_tab_btn[3];
static lv_obj_t* s_page[3];
static int s_tab = 0;

// One stepper row per Widget::Stepper bound, indexed like BOUNDS[].
static lv_obj_t* s_val[MAX_ROWS];
static lv_obj_t* s_minus[MAX_ROWS];
static lv_obj_t* s_plus[MAX_ROWS];

struct StepCtx { const Bound* b; int dir; };
static StepCtx s_ctx[MAX_ROWS * 2];

struct ChipCtx { const char* key; int value; };
static ChipCtx s_chips[kSampleN];
static lv_obj_t* s_chip_obj[kSampleN];

// take_toast() is one-shot (Commands clears it once read), so its message
// must be latched here together with an expiry, not re-read every refresh.
static char s_toast_msg[64] = {0};
static bool s_toast_active = false;
static uint32_t s_toast_started_ms = 0;

static void on_step(lv_event_t* e) {
    StepCtx* c = (StepCtx*)lv_event_get_user_data(e);
    const CoolerState& st = cooler_state();
    Commands& cmd = panel_commands();
    int cur = cmd.display_value(c->b->key, st);
    int step = c->b->step ? c->b->step : 1;
    cmd.request(c->b->key, cur + c->dir * step, platform_now_ms());
    screen_settings_refresh();
}

static void on_chip(lv_event_t* e) {
    ChipCtx* c = (ChipCtx*)lv_event_get_user_data(e);
    panel_commands().request(c->key, c->value, platform_now_ms());
    screen_settings_refresh();
}

static void show_tab(int t) {
    s_tab = t;
    for (int i = 0; i < 3; i++) {
        if (i == t) lv_obj_remove_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_tab_btn[i], i == t ? th_cool_bg() : th_surface(), 0);
    }
}

static void on_tab(lv_event_t* e) {
    show_tab((int)(intptr_t)lv_event_get_user_data(e));
    screen_settings_refresh();
}

static lv_obj_t* make_page(lv_obj_t* parent) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_size(p, 472, 480 - ROW0_Y - 4);
    lv_obj_set_pos(p, 0, ROW0_Y);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t* screen_settings_create(lv_obj_t* parent) {
    assert(BOUNDS_N <= MAX_ROWS && "screen_settings: BOUNDS[] outgrew the fixed-size UI arrays");

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_bg_color(s_root, th_page(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 4, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    for (int t = 0; t < 3; t++) {
        lv_obj_t* b = lv_button_create(s_root);
        lv_obj_set_size(b, 148, TAB_H);
        lv_obj_set_pos(b, 8 + t * 156, TAB_Y);
        lv_obj_set_style_radius(b, 20, 0);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, kTabNames[t]);
        lv_obj_set_style_text_color(l, th_ink(), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_tab, LV_EVENT_CLICKED, (void*)(intptr_t)t);
        s_tab_btn[t] = b;
        s_page[t] = make_page(s_root);
    }

    // Stepper rows, each on its group's page, in BOUNDS[] order.
    int y[3] = {0, 0, 0};
    int ci = 0;
    for (size_t i = 0; i < BOUNDS_N; i++) {
        const Bound* b = &BOUNDS[i];
        const int g = (int)b->group;
        lv_obj_t* page = s_page[g];

        if (b->widget == Widget::Preset) {
            // sampleinterval: stepping to an hour at 10 s a tap is absurd, so
            // it gets fixed chips. Every chip value sits inside the bound, so
            // Commands' clamp never silently turns a chip into another value.
            lv_obj_t* lbl = lv_label_create(page);
            lv_label_set_text(lbl, b->label);
            lv_obj_set_style_text_color(lbl, th_ink(), 0);
            lv_obj_set_pos(lbl, 8, y[g] + 10);
            for (int k = 0; k < kSampleN; k++) {
                lv_obj_t* chip = lv_button_create(page);
                lv_obj_set_size(chip, 58, 32);
                lv_obj_set_pos(chip, 150 + k * 64, y[g]);
                lv_obj_t* t = lv_label_create(chip);
                lv_obj_set_style_text_color(t, th_ink(), 0);
                lv_label_set_text(t, kSampleLbls[k]);
                lv_obj_center(t);
                s_chips[k] = ChipCtx{b->key, kSampleVals[k]};
                lv_obj_add_event_cb(chip, on_chip, LV_EVENT_CLICKED, &s_chips[k]);
                s_chip_obj[k] = chip;
            }
            y[g] += CHIP_ROW_H + 16;
            continue;
        }

        lv_obj_t* lbl = lv_label_create(page);
        lv_label_set_text(lbl, b->label);
        lv_obj_set_style_text_color(lbl, th_ink(), 0);
        lv_obj_set_pos(lbl, 8, y[g] + 16);

        s_val[i] = lv_label_create(page);
        lv_obj_set_style_text_color(s_val[i], th_ink2(), 0);
        lv_obj_set_pos(s_val[i], 200, y[g] + 16);

        for (int d = 0; d < 2; d++) {
            lv_obj_t* btn = lv_button_create(page);
            lv_obj_set_size(btn, 60, 48);
            lv_obj_set_pos(btn, d == 0 ? 330 : 400, y[g]);
            lv_obj_t* t = lv_label_create(btn);
            lv_label_set_text(t, d == 0 ? "-" : "+");
            lv_obj_center(t);
            s_ctx[ci] = StepCtx{b, d == 0 ? -1 : +1};
            lv_obj_add_event_cb(btn, on_step, LV_EVENT_CLICKED, &s_ctx[ci]);
            if (d == 0) s_minus[i] = btn; else s_plus[i] = btn;
            ci++;
        }
        y[g] += ROW_H;
    }

    for (int t = 0; t < 3; t++) {
        lv_obj_t* hint = lv_label_create(s_page[t]);
        lv_label_set_text(hint, kTabHints[t]);
        lv_obj_set_style_text_color(hint, th_muted(), 0);
        lv_obj_set_pos(hint, 8, y[t] + 12);
    }

    // Banner and toast share the top slot. Created last so they sit above the
    // pages in z-order.
    s_banner = lv_label_create(s_root);
    lv_obj_set_pos(s_banner, 8, BANNER_Y);
    lv_obj_set_width(s_banner, 464);
    lv_obj_set_style_bg_color(s_banner, th_warn(), 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_banner, th_page(), 0);
    lv_obj_set_style_pad_all(s_banner, 4, 0);
    lv_label_set_text(s_banner, "");
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

    s_toast = lv_label_create(s_root);
    lv_obj_set_pos(s_toast, 8, BANNER_Y);
    lv_obj_set_width(s_toast, 464);
    lv_obj_set_style_bg_color(s_toast, th_crit(), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_toast, th_ink(), 0);
    lv_obj_set_style_pad_all(s_toast, 4, 0);
    lv_label_set_text(s_toast, "");
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    show_tab(0);
    screen_settings_refresh();
    return s_root;
}

void screen_settings_refresh(void) {
    if (!s_root) return;
    const CoolerState& st = cooler_state();
    Commands& cmd = panel_commands();
    char buf[48];

    // Nothing authoritative to edit from until the first /data arrives.
    const bool ready = st.valid;
    if (!ready) {
        lv_label_set_text(s_banner, "waiting for data - settings read-only");
        lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    } else if (cooler_in_override(st)) {
        lv_label_set_text(s_banner, "override: fixed 3-5 C in force, set point waits");
        lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    }

    for (size_t i = 0; i < BOUNDS_N; i++) {
        const Bound* b = &BOUNDS[i];
        if (b->widget != Widget::Stepper || !s_val[i]) continue;
        int v = cmd.display_value(b->key, st);
        bool pending = cmd.is_pending(b->key);
        std::snprintf(buf, sizeof(buf), "%d %s%s", v, b->unit, pending ? " ..." : "");
        lv_label_set_text(s_val[i], buf);
        lv_obj_set_style_text_opa(s_val[i], (pending || !ready) ? LV_OPA_50 : LV_OPA_COVER, 0);
        if (ready) {
            lv_obj_remove_state(s_minus[i], LV_STATE_DISABLED);
            lv_obj_remove_state(s_plus[i], LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_minus[i], LV_STATE_DISABLED);
            lv_obj_add_state(s_plus[i], LV_STATE_DISABLED);
        }
    }

    for (int i = 0; i < kSampleN; i++) {
        int cur = cmd.display_value(s_chips[i].key, st);
        lv_obj_set_style_bg_color(s_chip_obj[i], cur == s_chips[i].value ? th_good() : th_surface(), 0);
        if (ready) lv_obj_remove_state(s_chip_obj[i], LV_STATE_DISABLED);
        else       lv_obj_add_state(s_chip_obj[i], LV_STATE_DISABLED);
    }

    char toast_buf[64];
    if (cmd.take_toast(toast_buf, sizeof(toast_buf))) {
        std::strncpy(s_toast_msg, toast_buf, sizeof(s_toast_msg) - 1);
        s_toast_msg[sizeof(s_toast_msg) - 1] = 0;
        s_toast_active = true;
        s_toast_started_ms = platform_now_ms();
    }
    if (s_toast_active && platform_now_ms() - s_toast_started_ms < TOAST_MS) {
        lv_label_set_text(s_toast, s_toast_msg);
        lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_toast);
    } else {
        s_toast_active = false;
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
}

void screen_settings_show_tab(int tab) {
    if (!s_root || tab < 0 || tab > 2) return;
    show_tab(tab);
    screen_settings_refresh();
}
```

Save this as `/tmp/task6-sim-tab.patch` and apply it from `CoolerPanel/` with `git apply /tmp/task6-sim-tab.patch` (it must apply cleanly; if it does not, the tree is not at the expected state -- stop and check the previous task):

````diff
diff --git a/sim/main.cpp b/sim/main.cpp
index 9a4853e..321d6a3 100644
--- a/sim/main.cpp
+++ b/sim/main.cpp
@@ -36,6 +36,7 @@ int main(int argc, char** argv) {
     const char* fixture = nullptr; // debug flag: --fixture FILE -- feed one /data
                                    // payload instead of connecting to the broker
     int page = -1;                // debug flag: --page 0|1|2 (Trend|Settings|Detail)
+    int tab = -1;                 // debug flag: --tab 0|1|2 (Settings: Box|Coil|Timing)
     for (int i = 1; i < argc; i++) {
         if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
         else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
@@ -43,6 +44,7 @@ int main(int argc, char** argv) {
         else if (!strcmp(argv[i], "--seed-history")) seed = true;
         else if (!strcmp(argv[i], "--fixture") && i + 1 < argc) fixture = argv[++i];
         else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = atoi(argv[++i]);
+        else if (!strcmp(argv[i], "--tab") && i + 1 < argc) tab = atoi(argv[++i]);
     }
     g_start = std::chrono::steady_clock::now();
     lv_init();
@@ -136,6 +138,10 @@ int main(int argc, char** argv) {
                                      // below already polls mqtt_poll() every tick.
             }
             if (page >= 0) nav_show_page(page);
+            if (tab >= 0) {
+                extern void screen_settings_show_tab(int);
+                screen_settings_show_tab(tab);
+            }
             ui_refresh();
         }
     }
````

- [ ] **Step 2: Build and check the tabs**

Run: BUILD, then `SHOT --fixture tests/fixtures/data_normal.json --page 1 --tab <t> --frames 1100 --screenshot /tmp/t6_<t>.png` for t = 0, 1, 2. Then again with `data_override.json` and `--tab 0`.

Expected:
- **Tab 0 (Box):** `Set point 4 C`, `Range +/- 2 C`, and a `Sample every` chip row with `1h` green.
- **Tab 1 (Coil):** `Ice cutoff 0 C`, `Ice clear 3 C`, `Settle 10 min`.
- **Tab 2 (Timing):** `Min off 5 min`, `Min run 180 s`, `Max run 10 min`, `Backup duty 50 %`.
- Each tab has its grey hint text below the rows. The selected tab is tinted.
- **Override:** an amber banner above the tabs reads `override: fixed 3-5 C in force, set point waits`, and the buttons stay enabled.

- [ ] **Step 3: Commit**

```bash
cd ~/Cooler/CoolerPanel
git add shared/ui/screen_settings.h shared/ui/screen_settings.cpp sim/main.cpp
git commit -m "feat(settings): Box / Coil / Timing tabs for the v2 settings; drop override toggle

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Detail screen with calibration, and alarm text

**Files:**
- Rewrite: `shared/ui/screen_detail.cpp`, `shared/ui/screen_alarm.cpp`

**Interfaces:**
- Consumes: `Commands::action("calibrate", 0|1)`, `Commands::action("fincal_reset", 1)`, `status_text` helpers, and the `AlarmId` values from Task 3.

- [ ] **Step 1: Implement**

`shared/ui/screen_detail.cpp`:

```cpp
#include "screen_detail.h"
#include "cooler_state.h"
#include "commands.h"
#include "status_text.h"
#include "app.h"
#include "screen_config.h"
#include "platform.h"
#include "theme.h"
#include <cstdio>

// Diagnostics for troubleshooting without a laptop at the AC: every run-state
// field /data carries, plus the one maintenance task the panel can drive --
// fin thermistor calibration (spec §2.7).
//
// Reset-to-defaults throws away a calibration that took a whole pull-down to
// collect, so it takes two taps: the first arms it for CONFIRM_MS, the second
// sends it.
#define CAL_Y       296
#define CONFIRM_MS  5000
#define FAIL_MS     4000   // how long a failed-send notice replaces the cal line

static lv_obj_t* s_root;
static lv_obj_t* s_body;
static lv_obj_t* s_cal_lbl;
static lv_obj_t* s_cal_btn;
static lv_obj_t* s_cal_btn_lbl;
static lv_obj_t* s_reset_btn;
static lv_obj_t* s_reset_lbl;
static bool s_reset_armed = false;
static uint32_t s_reset_armed_ms = 0;
// Commands' failure toast is shown by the Settings page, which is not the one
// on screen here -- so a failed action is also flagged locally.
static bool s_fail = false;
static uint32_t s_fail_ms = 0;

static void send(const char* key, int value) {
    if (!panel_commands().action(key, value)) { s_fail = true; s_fail_ms = platform_now_ms(); }
}

static void on_cal(lv_event_t*) {
    const CoolerState& st = cooler_state();
    send("calibrate", st.cal_active ? 0 : 1);
    screen_detail_refresh();
}

static void on_reset(lv_event_t*) {
    const uint32_t now = platform_now_ms();
    if (s_reset_armed && now - s_reset_armed_ms < CONFIRM_MS) {
        send("fincal_reset", 1);
        s_reset_armed = false;
    } else {
        s_reset_armed = true;
        s_reset_armed_ms = now;
    }
    screen_detail_refresh();
}

static lv_obj_t* small_button(lv_obj_t* parent, int x, int w, lv_obj_t** lbl, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, w, 44);
    lv_obj_set_pos(b, x, CAL_Y + 32);
    lv_obj_set_style_bg_color(b, th_surface(), 0);
    *lbl = lv_label_create(b);
    lv_obj_set_style_text_color(*lbl, th_ink(), 0);
    lv_obj_center(*lbl);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

lv_obj_t* screen_detail_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_bg_color(s_root, th_page(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_body = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_body, th_ink(), 0);
    lv_obj_set_pos(s_body, 12, 12);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_body, 456);

    s_cal_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_cal_lbl, th_ink2(), 0);
    lv_obj_set_pos(s_cal_lbl, 12, CAL_Y);
    s_cal_btn = small_button(s_root, 12, 200, &s_cal_btn_lbl, on_cal);
    s_reset_btn = small_button(s_root, 224, 220, &s_reset_lbl, on_reset);

    // Entry point to the WiFi/MQTT form. This page is where you come when
    // something looks wrong, so it is where the "change the broker" door
    // belongs -- not buried behind a 4.5s boot-splash gear tap.
    lv_obj_t* cfg = lv_button_create(s_root);
    lv_obj_set_size(cfg, 220, 46);
    lv_obj_align(cfg, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_bg_color(cfg, th_surface(), 0);
    lv_obj_t* cl = lv_label_create(cfg);
    lv_label_set_text(cl, "WiFi / MQTT setup");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cfg, [](lv_event_t*) { screen_config_open(); },
                        LV_EVENT_CLICKED, nullptr);

    screen_detail_refresh();
    return s_root;
}

void screen_detail_refresh(void) {
    if (!s_root) return;
    const CoolerState& s = cooler_state();
    char state[32], hold[16], up[16], coil[48], cal[48], buf[640];

    fmt_state_value(s, state, sizeof(state));
    fmt_dur(s.hold_s, hold, sizeof(hold));
    fmt_dur(s.uptime_s, up, sizeof(up));
    if (s.fin_temp_valid)
        std::snprintf(coil, sizeof(coil), "%.1f C  %.0f ohm  %+.1f C/min",
                      (double)s.fin_temp, (double)s.fin_ohms, (double)s.fin_slope);
    else
        std::snprintf(coil, sizeof(coil), "-- (sensor fault)");
    if (s.fin_cal_err_valid)
        std::snprintf(cal, sizeof(cal), "%s  beta %.0f  r0 %.0f  err %.2f C",
                      s.fin_cal ? "ok" : "default", (double)s.fin_beta, (double)s.fin_r0,
                      (double)s.fin_cal_err);
    else
        std::snprintf(cal, sizeof(cal), "%s  beta %.0f  r0 %.0f",
                      s.fin_cal ? "ok" : "default", (double)s.fin_beta, (double)s.fin_r0);

    std::snprintf(buf, sizeof(buf),
        "mode      %s   override %s\n"
        "state     %s   hold %s\n"
        "relay %d   call %d   compressor %s\n"
        "coil      %s\n"
        "fin cal   %s\n"
        "faults    box %d   coil %d   no-response %d\n"
        "uptime    %s   link %s\n"
        "last rx   %llds ago",
        fmt_mode_label(s), s.override_src[0] ? s.override_src : "--",
        state, hold,
        s.relay, s.cool_call, fmt_compressor(s),
        coil,
        cal,
        s.sht_fault, s.fin_fault, s.no_response,
        up, s.online ? "online" : "OFFLINE",
        (long long)(platform_epoch_utc() - s.last_rx_epoch));
    lv_label_set_text(s_body, buf);

    // Calibration: progress while running, otherwise what a run needs.
    if (s_fail && platform_now_ms() - s_fail_ms >= FAIL_MS) s_fail = false;
    lv_obj_set_style_text_color(s_cal_lbl, s_fail ? th_crit() : th_ink2(), 0);
    if (s_fail)
        std::snprintf(buf, sizeof(buf), "Send failed - check the MQTT link");
    else if (s.cal_active)
        std::snprintf(buf, sizeof(buf), "Calibrating: %d points, span %.1f C (need 4 / 8 C)",
                      s.cal_points, (double)s.cal_span);
    else
        std::snprintf(buf, sizeof(buf), "Fin calibration: best started warm, at a pull-down");
    lv_label_set_text(s_cal_lbl, buf);
    lv_label_set_text(s_cal_btn_lbl, s.cal_active ? "Abort calibration" : "Start calibration");
    lv_obj_set_style_bg_color(s_cal_btn, s.cal_active ? th_warn() : th_surface(), 0);

    if (s_reset_armed && platform_now_ms() - s_reset_armed_ms >= CONFIRM_MS) s_reset_armed = false;
    lv_label_set_text(s_reset_lbl, s_reset_armed ? "Tap again to reset" : "Reset calibration");
    lv_obj_set_style_bg_color(s_reset_btn, s_reset_armed ? th_crit() : th_surface(), 0);

    if (s.valid) {
        lv_obj_remove_state(s_cal_btn, LV_STATE_DISABLED);
        lv_obj_remove_state(s_reset_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_cal_btn, LV_STATE_DISABLED);
        lv_obj_add_state(s_reset_btn, LV_STATE_DISABLED);
    }
}
```

`shared/ui/screen_alarm.cpp`:

```cpp
#include "screen_alarm.h"
#include "alarm.h"
#include "cooler_state.h"
#include "app.h"
#include "platform.h"
#include "theme.h"
#include <cstdio>
#include <cstring>

// Full-screen takeover: hidden whenever no condition is active, and raised
// above everything else (including whatever Task 13's tileview later adds)
// the moment one is. Parented directly to the nav root rather than into any
// page/tile -- an alarm must interrupt the operator regardless of which
// screen they're currently looking at.
static lv_obj_t* s_root;
static lv_obj_t* s_title;
static lv_obj_t* s_detail;
static AlarmId s_showing = AlarmId::None;

static void on_ack(lv_event_t*) {
    panel_alarms().acknowledge(s_showing, platform_epoch_utc());
    screen_alarm_refresh();
}

lv_obj_t* screen_alarm_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    // th_crit() is the theme's critical/red token (see screen_settings.cpp's
    // failure toast) -- reused here rather than inventing a bespoke dark red.
    lv_obj_set_style_bg_color(s_root, th_crit(), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    // Titles like "AC NOT RESPONDING" are wider than the panel at 48 px, so
    // the title wraps inside a fixed width instead of running off both edges.
    s_title = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_title, th_ink(), 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_title, 440);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 50);

    s_detail = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_detail, th_ink(), 0);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail, 420);
    lv_obj_align(s_detail, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t* btn = lv_button_create(s_root);
    lv_obj_set_size(btn, 240, 64);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(btn, th_surface(), 0);
    lv_obj_t* t = lv_label_create(btn);
    lv_label_set_text(t, "ACKNOWLEDGE");
    lv_obj_set_style_text_color(t, th_ink(), 0);
    lv_obj_center(t);
    lv_obj_add_event_cb(btn, on_ack, LV_EVENT_CLICKED, nullptr);
    return s_root;
}

void screen_alarm_refresh(void) {
    if (!s_root) return;
    const CoolerState& st = cooler_state();
    AlarmId id = panel_alarms().active(platform_epoch_utc());
    s_showing = id;
    if (id == AlarmId::None) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
    lv_label_set_text(s_title, Alarms::text(id));

    char buf[160];
    switch (id) {
        case AlarmId::ControllerSilent:
            std::snprintf(buf, sizeof(buf), "no message for %d min",
                          (int)((platform_epoch_utc() - st.last_rx_epoch) / 60));
            break;
        case AlarmId::NoResponse:
            std::snprintf(buf, sizeof(buf),
                          "cooling requested for 10 min, compressor not running.\n"
                          "Check AC power, mode, relay and CN3 wiring.");
            break;
        case AlarmId::BoxSensorFault:
            std::snprintf(buf, sizeof(buf), "%s",
                          std::strcmp(st.mode, "blind") == 0 ? "cooling on timer only"
                          : std::strstr(st.mode, "proxy")    ? "cooling on fin sensor"
                                                              : "box temperature lost");
            break;
        case AlarmId::FinSensorFault:
            std::snprintf(buf, sizeof(buf), "icing protection on timer backstop");
            break;
        case AlarmId::NotKeepingUp:
            if (cooler_in_override(st))
                std::snprintf(buf, sizeof(buf), "%.1f vs override 5", (double)st.temp);
            else
                std::snprintf(buf, sizeof(buf), "%.1f vs set %d", (double)st.temp, st.coolerset);
            break;
        default:
            std::snprintf(buf, sizeof(buf), "mode %s", st.mode);
            break;
    }
    lv_label_set_text(s_detail, buf);
}
```

- [ ] **Step 2: Build and check**

Run: BUILD, then:
- `SHOT --fixture tests/fixtures/data_normal.json --page 2 --frames 1100 --screenshot /tmp/t7_detail.png`
- the same with `data_calibrating.json`
- `SHOT --fixture tests/fixtures/data_noresponse.json --frames 1100 --screenshot /tmp/t7_nr.png`
- the same with `data_finproxy.json`

Expected:
- **Detail, normal:** eight diagnostic lines (`mode NORMAL override none`, `state Cooling 2m hold 38s`, …, `fin cal ok beta 3912 r0 10240 err 0.30 C`), then `Start calibration` / `Reset calibration` buttons and `WiFi / MQTT setup`.
- **Detail, calibrating:** `Calibrating: 3 points, span 6.2 C (need 4 / 8 C)`, with an amber `Abort calibration` button.
- **Alarm, noresponse:** red takeover. `AC NOT RESPONDING` wraps onto two centred lines, and the two-line hint mentions CN3 wiring.
- **Alarm, finproxy:** `BOX SENSOR FAULT` over `cooling on fin sensor`.
- No title runs off the screen edge.

- [ ] **Step 3: Tap test in the interactive sim**

Run `./build/v4/cooler_sim --fixture tests/fixtures/data_normal.json --page 2` (with a window). Tap `Reset calibration` once: it turns red and reads `Tap again to reset`. Wait 6 s: it reverts. With no broker connected, `Start calibration` makes the calibration line read `Send failed - check the MQTT link` in red for about 4 s (Review Focus #5).

- [ ] **Step 4: Commit**

```bash
cd ~/Cooler/CoolerPanel
git add shared/ui/screen_detail.cpp shared/ui/screen_alarm.cpp
git commit -m "feat(detail): v2 diagnostics and fin calibration controls; readable alarm titles

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Delete the legacy v3 fields

**Files:**
- Modify: `shared/model/cooler_state.h`, `shared/model/history.h`, `shared/ui/chart.h`, `README.md`

- [ ] **Step 1: Apply**

Save this as `/tmp/task8-legacy.patch` and apply it from `CoolerPanel/` with `git apply /tmp/task8-legacy.patch` (it must apply cleanly; if it does not, the tree is not at the expected state -- stop and check the previous task):

````diff
diff --git a/shared/model/cooler_state.h b/shared/model/cooler_state.h
index 9bda6f4..d0b8041 100644
--- a/shared/model/cooler_state.h
+++ b/shared/model/cooler_state.h
@@ -48,17 +48,6 @@ struct CoolerState {
 
     uint32_t uptime_s = 0;
 
-    // ---- LEGACY v3 fields: no longer parsed, always zero. Kept only so the
-    //      screens still compile until they are rewritten; the final task of
-    //      the v4 panel plan deletes this block.
-    int screentimeout = 0, override_on = 0, lead = 0, active_unit = 0, pot_pct = 0;
-    bool ac_on = false, in_duty = false;
-    char ac1[8] = {0}, ac2[8] = {0};
-    bool ac1_relay = false, ac2_relay = false;
-    int ac1_blocked = 0, ac2_blocked = 0;
-    uint32_t ac1_runtime_s = 0, ac2_runtime_s = 0;
-    int window_s = 0, window_pos_s = 0, window_on_s = 0, swap_in_s = 0;
-
     // ---- panel-side meta ----
     int64_t last_rx_epoch = 0;
     bool valid = false;
diff --git a/shared/model/history.h b/shared/model/history.h
index 5470e2c..6ab38d8 100644
--- a/shared/model/history.h
+++ b/shared/model/history.h
@@ -12,7 +12,8 @@ struct Sample {
     int64_t t;          // epoch seconds
     int16_t temp_c10;   // degrees C x10
     int16_t rh_c10;     // %RH x10
-    uint8_t ac;         // unit running at this sample: 0 none, 1, 2 (/data active_unit)
+    uint8_t ac;         // 1 = relay closed (cooling requested) at this sample, else 0.
+                        // 2 was AC 2 under the v3 controller; nothing records it now.
 };
 
 class History {
diff --git a/shared/ui/chart.h b/shared/ui/chart.h
index 5373e81..7a1fecd 100644
--- a/shared/ui/chart.h
+++ b/shared/ui/chart.h
@@ -7,11 +7,10 @@ class History;
 // plain average would erase. `has` is false when no sample fell in the
 // column -- render a gap, never interpolate across a data outage.
 //
-// `ac` is the unit that ran for most of the column, which is what the bar
-// gets coloured by. A column is 35 s wide at the 1 h zoom and ~100 min at 7 d,
-// so at the long zooms it reports the balance of a period the units alternated
-// through rather than a single run -- still the useful reading (which unit is
-// carrying the load), just coarser.
+// `ac` is 1 when the relay was closed for most of the column (see
+// History's Sample::ac), which is what the bar gets coloured by. A column is
+// 35 s wide at the 1 h zoom and ~100 min at 7 d, so at the long zooms it
+// reports whether the column was mostly cooling rather than a single run.
 struct Column {
     float tmin, tmax;
     float hmin, hmax;
diff --git a/README.md b/README.md
index 806f4b3..552ec5f 100644
--- a/README.md
+++ b/README.md
@@ -79,43 +79,25 @@ Two tests must pass:
 
 ## 5. Screenshots
 
-Headless (no real window/X server needed — used in CI and by every task in
-this project):
+Headless (no window or X server needed):
 
 ```bash
-SDL_VIDEODRIVER=dummy ./firmware/build/llmmon_sim \
-    --replay firmware/tests/fixtures --frames 5 --now 1785410700 \
-    --screenshot firmware/build/dashboard.png
+SDL_VIDEODRIVER=dummy ./build/cooler_sim --seed-history \
+    --fixture tests/fixtures/data_normal.json --page 0 \
+    --frames 1100 --screenshot build/trend.png
 ```
 
-- `--replay DIR` feeds the six JSON fixtures in that directory through
-  `app_on_mqtt_message` directly (no network/broker needed) so the dashboard
-  renders deterministic, checked-in sample data.
-- `--frames N` runs exactly N display-timer ticks then exits (instead of
-  looping forever).
-- `--screenshot PATH` snapshots the active screen to a 480×480 PNG on exit.
-- `--now EPOCH_SECONDS` pins `platform_epoch_utc()` to a fixed time; it's
-  needed here because the vendor-status fixtures carry a fixed `ts` of
-  `2026-07-30T11:20:00Z` (epoch `1785410400`) and the staleness gate in
-  `fmt_vendor_status()` (`firmware/shared/format.cpp`) renders "Status
-  unknown" once a payload's `ts` is more than 1200 s behind the real clock, so
-  without `--now` this command's vendor-status row ages out and stops being
-  deterministic a few minutes after these fixtures were written. `1785410700`
-  is 5 minutes after the fixtures' `ts`, comfortably inside that window.
-
-Two debug-only flags open a specific detail surface over the replayed data,
-for screenshotting states that normally only appear after a touch gesture:
-
-```bash
---card session|stats      # open the session or stats detail card (Claude Code)
---screen boot|expanded|system   # open the boot splash / expanded view / system status
-```
-
-Gestures themselves (drag-scroll, double-tap, long-press) can't be captured by
-`--screenshot` since they require real touch/mouse timing — see
-`.superpowers/sdd/task-10-report.md` for how those were verified (a headless
-synthetic-touch harness driving the real LVGL input pipeline) instead of
-relied on the screenshot flags above.
+- `--fixture FILE` routes one captured `/data` payload (plus an `online`
+  availability) through `app_on_mqtt_message` and does **not** connect to the
+  broker. `tests/fixtures/data_*.json` covers each v4 controller state:
+  normal, defrost, override, finproxy, blind, noresponse, calibrating.
+- `--seed-history` fills the trend with a synthetic week first.
+- `--page 0|1|2` opens Trend / Settings / Detail; `--tab 0|1|2` picks the
+  Settings tab (Box / Coil / Timing).
+- `--frames N` runs N main-loop iterations (5 ms each) then exits. 1100 clears
+  the 4.5 s boot splash.
+- `--screenshot PATH` snapshots the screen to a 480x480 PNG on exit.
+- A fixture with a fault flag raises its alarm takeover, which covers the page.
 
 ## 6. Portability rule
 
````

- [ ] **Step 2: Prove nothing reads them**

Run: BUILD
Expected: builds with no errors and `100% tests passed`. A compile error here names a v3 field something still uses. Fix that call site, don't restore the field.

Run: `grep -rn "override_on\|active_unit\|pot_pct\|in_duty\|ac_on\|swap_in_s\|window_s\|screentimeout" shared sim device/src --include=*.cpp --include=*.h`
Expected: no output.

- [ ] **Step 3: Commit**

```bash
cd ~/Cooler/CoolerPanel
git add shared/model/cooler_state.h shared/model/history.h shared/ui/chart.h README.md
git commit -m "refactor: drop v3 dual-AC fields; document the screenshot flags

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Device build and live check

- [ ] **Step 1: Build the device firmware**

Run: `cd ~/Cooler/CoolerPanel/device && pio run`
Expected: `[SUCCESS]`. `device/src/assets_gen/` must already exist; if not, run `tools/gen_assets.sh` first (see `device/README.md`).

- [ ] **Step 2: Flash** with `pio run -t upload`, connected to the USB-to-UART port.

- [ ] **Step 3: Live check against the v4 controller** (needs the companion plan's Task 4 done and the controller publishing):
  - The Trend screen shows real temperature, coil, status and AC UNIT values, and the header age stays under about 35 s.
  - On Settings → Box, change Set point by 1. The value shows ` ...`, then settles within 3 s. `mosquitto_sub -t ha/esp32-cooler/data` shows the new `coolerset`.
  - On Settings → Coil, set Ice cutoff to 4. Ice clear is bumped to 5, both on the panel and in `/data`. Set them back to 0 and 3.
  - Close the controller's override switch. The Trend screen shows `OVERRIDE 3-5  SWITCH` within one publish, and Settings shows the amber banner.
  - On Detail, tap `Start calibration`. `cal_active` goes to 1 in `/data` and the button turns amber. Tap `Abort calibration`.

- [ ] **Step 4: Update the fixtures from the real controller** (so they match what it actually publishes): `COOLER_MQTT_PASS=… tests/capture_fixtures.sh normal`. Rerun BUILD. If a test fails on a real-vs-hand-written difference, fix the parser or the test, whichever is wrong. Commit any changed fixtures:

```bash
cd ~/Cooler/CoolerPanel
git add tests/fixtures
git commit -m "test: refresh v2 fixtures from the live controller

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
