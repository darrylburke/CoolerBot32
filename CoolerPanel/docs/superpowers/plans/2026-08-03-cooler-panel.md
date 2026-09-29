# CoolerPanel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Waveshare Smart 86 (ESP32-S3, 480×480) touch panel that monitors and configures the ESPHome walk-in cooler over MQTT.

**Architecture:** Fork LLMMon's three-layer firmware tree. `shared/` holds all portable logic and LVGL UI and builds for both the PC simulator and the ESP32-S3; `sim/` is the SDL desktop platform and the primary development loop; `device/` is the ESP32-S3 platform layer, carried over unchanged. The one new capability versus LLMMon is an outbound MQTT publish path added to the platform seam.

**Tech Stack:** C++17, CMake 3.20+ (FetchContent), LVGL 9.3.0, ArduinoJson 7.4.3, doctest 2.4.11, libmosquitto (sim), PubSubClient (device), PlatformIO/Arduino-ESP32 (device).

## Global Constraints

- **`shared/` must call only LVGL.** `tests/portability_guard.sh` greps for `SDL_`, `mosquitto`, `freetype`, `ft2build`, `stb_image`, `MQTTClient`, `MQTTAsync` under `shared/` and fails the build if any appear. Platform access goes through `shared/platform.h` only.
- **Broker:** `mqtt.example.com:1883`, plain MQTT, the broker user, password `<REDACTED - see tools/import_cooler_broker.sh>`. No TLS.
- **Topic prefix:** `ha/esp32-cooler`. Subscribe `.../data` and `.../availability`. Publish `.../cmd`. Do **not** subscribe `.../ac1` or `.../ac2`.
- **The cooler is authoritative.** It clamps every write. The panel renders what `/data` reports, never what it requested.
- **Min/max/step limits live in exactly one place** — `shared/model/bounds.cpp`. UI widgets and the command clamp both read it. (The preset *choices* offered for `screentimeout`/`sampleinterval` in Task 15 are UI presentation, not limits, and correctly live with the widget — but every preset value must still fall inside its `bounds.cpp` range.)
- **ArduinoJson 7 API:** use `doc["k"].is<T>()` and `doc["k"].as<T>()`. `containsKey()` is deprecated.
- **Screen coordinate space is 480×480.**
- **Naming wart to preserve:** the cooler accepts `sampleinterval` on `/cmd` but publishes the same value as `hist_interval_s` on `/data`. Parse one, send the other.
- **Test fixture macro is `FIXture_DIR`** (that capitalisation, inherited from LLMMon's CMakeLists).

---

## File Structure

| File | Responsibility |
|---|---|
| `shared/platform.h` | Seam: time + outbound publish |
| `shared/app.h` / `app.cpp` | Global state accessors, inbound message entry point |
| `shared/model/bounds.{h,cpp}` | Value limits, widget kinds, clamping |
| `shared/model/cooler_state.{h,cpp}` | `/data` payload → struct |
| `shared/model/mqtt_router.{h,cpp}` | Topic dispatch |
| `shared/model/history.{h,cpp}` | PSRAM ring buffer of samples |
| `shared/model/commands.{h,cpp}` | Outbound builder, debounce, reconciliation |
| `shared/model/alarm.{h,cpp}` | Alarm state machine |
| `shared/ui/chart.{h,cpp}` | Downsampling + trend rendering |
| `shared/ui/screen_trend.cpp` | Default screen |
| `shared/ui/screen_settings.cpp` | Stepper/preset rows |
| `shared/ui/screen_detail.cpp` | Read-only diagnostics |
| `shared/ui/screen_alarm.cpp` | Full-screen takeover |
| `shared/ui/nav.{h,cpp}` | Swipe paging |
| `shared/ui/theme.{h,cpp}` | Colour/type tokens (from LLMMon) |
| `sim/mqtt_mosq.cpp` | libmosquitto + publish impl |
| `device/src/mqtt_pubsub.cpp` | PubSubClient + publish impl |
| `tests/*.cpp` | doctest suites |
| `tests/fixtures/*.json` | Captured real payloads |

---

# Phase A — Fork and model foundation

### Task 1: Fork the tree and get a green build

**Files:**
- Create: entire `~/projects/CoolerPanel/` tree (copied from `~/projects/LLMMon/firmware/`)
- Modify: `CMakeLists.txt`, `tests/portability_guard.sh`
- Delete: LLMMon-specific screens, model, and tests

**Interfaces:**
- Consumes: nothing
- Produces: a building CMake project with targets `cooler_shared`, `cooler_config`, `cooler_sim`, `cooler_tests`

- [ ] **Step 1: Copy the firmware tree into the repo root**

```bash
cd ~/projects/CoolerPanel
rsync -a --exclude build/ --exclude .git/ ~/projects/LLMMon/firmware/ ./
ls   # shared sim device config tests cmake CMakeLists.txt
```

- [ ] **Step 2: Delete LLMMon-specific application code**

```bash
cd ~/projects/CoolerPanel
rm -f shared/model/app_model.cpp shared/model/app_model.h
rm -f shared/ui/screen_dashboard.cpp shared/ui/screen_expanded.cpp \
      shared/ui/screen_session.cpp shared/ui/screen_stats.cpp \
      shared/ui/screen_system.cpp
rm -f tests/test_model.cpp tests/test_router.cpp tests/test_dashboard.cpp
rm -f tests/fixtures/*.json
rm -f shared/model/mqtt_router.cpp shared/model/mqtt_router.h
```

Keep: `shared/ui/screen_boot.cpp`, `shared/ui/screen_setup.cpp`, `shared/ui/theme.{h,cpp}`, `shared/ui/nav.{h,cpp}`, `shared/ui/ui.{h,cpp}`, `shared/format.{h,cpp}`, `shared/platform.h`, `shared/app.{h,cpp}`, all of `sim/`, `device/`, `config/`, `cmake/`.

- [ ] **Step 3: Write the new CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(cooler_panel CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/deps.cmake)

find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)
find_library(MOSQUITTO_LIB mosquitto REQUIRED)

file(GLOB_RECURSE SHARED_SRC CONFIGURE_DEPENDS shared/*.cpp)
add_library(cooler_shared STATIC ${SHARED_SRC})
target_include_directories(cooler_shared PUBLIC shared shared/model shared/ui)
target_link_libraries(cooler_shared PUBLIC lvgl ArduinoJson)

add_library(cooler_config STATIC config/device_config.cpp)
target_include_directories(cooler_config PUBLIC config)
target_link_libraries(cooler_config PUBLIC ArduinoJson)

file(GLOB SIM_SRC CONFIGURE_DEPENDS sim/*.cpp)
add_executable(cooler_sim ${SIM_SRC})
target_include_directories(cooler_sim PRIVATE sim sim/third_party
    ${SDL2_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(cooler_sim PRIVATE cooler_shared lvgl
    ${SDL2_LIBRARIES} ${FREETYPE_LIBRARIES} ${MOSQUITTO_LIB})

enable_testing()
add_executable(cooler_tests
    tests/test_format.cpp
    config/tests/test_device_config.cpp)
target_link_libraries(cooler_tests PRIVATE cooler_shared cooler_config lvgl doctest::doctest)
target_compile_definitions(cooler_tests PRIVATE FIXture_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME unit COMMAND cooler_tests)
add_test(NAME portability_guard COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/portability_guard.sh)
```

- [ ] **Step 4: Stub out the removed model so `app.cpp` and the kept screens still link**

Read `shared/app.cpp`, `shared/ui/ui.cpp`, `shared/ui/nav.cpp`, `shared/ui/screen_boot.cpp`, `shared/ui/screen_setup.cpp`. Delete every reference to `AppModel`, `app_model()`, `Session`, `CliState`, `nav_open_session`, `nav_open_stats`, `nav_open_expanded`, `nav_open_system`. Reduce `shared/app.h` to:

```cpp
#pragma once
#include "platform.h"
#ifdef __cplusplus
extern "C" {
#endif
void app_on_mqtt_message(const char* topic, const uint8_t* payload, size_t len);
void app_set_link_state(link_state_t s);
link_state_t app_link_state(void);
#ifdef __cplusplus
}
#endif
```

Reduce `shared/ui/nav.h` to:

```cpp
#pragma once
#include "lvgl.h"
void nav_init(lv_obj_t* root);
```

- [ ] **Step 5: Enable the LVGL fonts and widgets this plan uses**

LLMMon's `shared/lv_conf.h` enables only `LV_FONT_MONTSERRAT_14` and has no
tileview setting. Tasks 7, 12 and 13 need a 48 px font and `lv_tileview`, so
they will not compile without this. In **both** `shared/lv_conf.h` and
`device/lv_conf.h`, ensure:

```c
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_USE_CANVAS   1
#define LV_USE_TILEVIEW 1
#define LV_USE_BUTTON   1
#define LV_USE_LABEL    1
```

Add any line that is absent; leave existing `1` values alone. The 48 px font
costs roughly 30 KB of flash, which is affordable on a 16 MB part.

- [ ] **Step 6: Configure and build**

Run:
```bash
cd ~/projects/CoolerPanel
cmake -S . -B build && cmake --build build -j
```
Expected: builds clean. Fix any dangling references until it does.

- [ ] **Step 7: Run the test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: `unit` and `portability_guard` both PASS.

- [ ] **Step 8: Commit**

```bash
cd ~/projects/CoolerPanel
git add -A
git commit -m "chore: fork LLMMon firmware tree, strip app layer, green build"
```

---

### Task 2: Capture replay fixtures from the live cooler

**Files:**
- Create: `tests/fixtures/data_normal.json`, `tests/fixtures/data_override.json`, `tests/fixtures/data_fault.json`, `tests/fixtures/data_malformed.json`, `tests/capture_fixtures.sh`

**Interfaces:**
- Consumes: nothing
- Produces: fixture files consumed by Tasks 4, 9, 11

- [ ] **Step 1: Write the capture script**

Create `tests/capture_fixtures.sh`:

```bash
#!/usr/bin/env bash
# Capture live /data payloads from the cooler into tests/fixtures/.
set -euo pipefail
H=mqtt.example.com; U=<user>; P=<REDACTED - see tools/import_cooler_broker.sh>
T=ha/esp32-cooler/data
D="$(cd "$(dirname "$0")" && pwd)/fixtures"
mkdir -p "$D"
name="${1:?usage: capture_fixtures.sh <name>}"
mosquitto_sub -h "$H" -p 1883 -u "$U" -P "$P" -t "$T" -C 1 > "$D/data_$name.json"
echo "wrote $D/data_$name.json ($(wc -c < "$D/data_$name.json") bytes)"
```

```bash
chmod +x tests/capture_fixtures.sh
```

- [ ] **Step 2: Capture the normal-operation fixture**

Run: `./tests/capture_fixtures.sh normal`

This is the known-good baseline. It must look like the payload below (values will differ; the *keys* must all be present):

```json
{"date":1785795756,"uptime_s":4878,"temp":15.523,"humidity":66.35233,
 "coolerset":4,"range":1,"maxrun":15,"minofftime":5,"dutypercent":80,
 "ac1":"On","ac2":"Off","ac_on":true,"in_duty":true,"mode":"normal",
 "lead":1,"active_unit":1,"cool_call":1,"override":0,"sht_fault":0,
 "pot_pct":37,"minruntime":180,"screentimeout":300,
 "hist_n":1,"hist_interval_s":3600,"hist_last_ts":1785794605,
 "temp_hist":[16],"hum_hist":[64],
 "window_s":900,"window_pos_s":671,"window_on_s":720,"swap_in_s":229,
 "ac1_relay":true,"ac2_relay":false,"ac1_blocked":0,"ac2_blocked":0,
 "ac1_runtime_s":1871,"ac2_runtime_s":1627}
```

- [ ] **Step 3: Capture the override fixture**

```bash
mosquitto_pub -h mqtt.example.com -p 1883 -u <user> -P <REDACTED - see tools/import_cooler_broker.sh> \
  -t ha/esp32-cooler/cmd -m '{"override":1}'
sleep 3
./tests/capture_fixtures.sh override
mosquitto_pub -h mqtt.example.com -p 1883 -u <user> -P <REDACTED - see tools/import_cooler_broker.sh> \
  -t ha/esp32-cooler/cmd -m '{"override":0}'
```

Verify `data_override.json` contains `"mode":"override"` and `"override":1`.

- [ ] **Step 4: Hand-build the fault and malformed fixtures**

A real SHT30 fault can't be induced safely. Copy `data_normal.json` to `data_fault.json` and edit `"sht_fault":0` → `"sht_fault":1` and `"mode":"normal"` → `"mode":"sht30-failsafe"`.

Create `tests/fixtures/data_malformed.json` containing exactly:

```
{"temp":15.5,"coolerset":
```

- [ ] **Step 5: Commit**

```bash
git add tests/capture_fixtures.sh tests/fixtures/
git commit -m "test: capture live cooler MQTT fixtures"
```

---

### Task 3: Bounds table

**Files:**
- Create: `shared/model/bounds.h`, `shared/model/bounds.cpp`, `tests/test_bounds.cpp`
- Modify: `CMakeLists.txt` (add `tests/test_bounds.cpp` to `cooler_tests`)

**Interfaces:**
- Consumes: nothing
- Produces: `enum class Widget`, `struct Bound`, `BOUNDS[]`, `BOUNDS_N`, `bounds_find(const char*) -> const Bound*`, `bounds_clamp(const char* key, int v, int cur_minofftime) -> int`

- [ ] **Step 1: Write the failing test**

Create `tests/test_bounds.cpp`:

```cpp
#include <doctest/doctest.h>
#include "bounds.h"
#include <cstring>

TEST_CASE("every bound is well formed") {
    REQUIRE(BOUNDS_N == 8);
    for (size_t i = 0; i < BOUNDS_N; i++) {
        CHECK(BOUNDS[i].key != nullptr);
        CHECK(BOUNDS[i].label != nullptr);
        CHECK(BOUNDS[i].lo <= BOUNDS[i].hi);
    }
}

TEST_CASE("find locates keys and rejects unknowns") {
    REQUIRE(bounds_find("coolerset") != nullptr);
    CHECK(bounds_find("coolerset")->hi == 40);
    CHECK(bounds_find("nonsense") == nullptr);
}

TEST_CASE("clamp holds each value inside its range") {
    CHECK(bounds_clamp("coolerset", 1, 5) == 2);
    CHECK(bounds_clamp("coolerset", 2, 5) == 2);
    CHECK(bounds_clamp("coolerset", 40, 5) == 40);
    CHECK(bounds_clamp("coolerset", 41, 5) == 40);
    CHECK(bounds_clamp("range", -1, 5) == 0);
    CHECK(bounds_clamp("range", 6, 5) == 5);
    CHECK(bounds_clamp("dutypercent", 1, 5) == 5);
    CHECK(bounds_clamp("dutypercent", 101, 5) == 100);
    CHECK(bounds_clamp("minruntime", -5, 5) == 0);
    CHECK(bounds_clamp("minruntime", 601, 5) == 600);
    CHECK(bounds_clamp("minofftime", 31, 5) == 30);
}

TEST_CASE("maxrun floor tracks the current minofftime") {
    // maxrun must be strictly greater than minofftime
    CHECK(bounds_clamp("maxrun", 3, 5) == 6);
    CHECK(bounds_clamp("maxrun", 6, 5) == 6);
    CHECK(bounds_clamp("maxrun", 20, 5) == 20);
    CHECK(bounds_clamp("maxrun", 61, 5) == 60);
    // with minofftime at its own ceiling the floor is 31
    CHECK(bounds_clamp("maxrun", 10, 30) == 31);
}

TEST_CASE("unknown keys pass through unchanged") {
    CHECK(bounds_clamp("nonsense", 12345, 5) == 12345);
}
```

- [ ] **Step 2: Add the test to the build and run it to verify it fails**

In `CMakeLists.txt`, add `tests/test_bounds.cpp` to the `cooler_tests` source list.

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL — `bounds.h: No such file or directory`

- [ ] **Step 3: Write the header**

Create `shared/model/bounds.h`:

```cpp
#pragma once
#include <stddef.h>

// How the settings screen renders this value.
enum class Widget { Stepper, Preset, Toggle };

struct Bound {
    const char* key;     // MQTT command key
    const char* label;   // UI label
    const char* unit;    // display unit, "" for none
    int lo;
    int hi;
    int step;            // 0 for Preset/Toggle widgets
    Widget widget;
};

extern const Bound BOUNDS[];
extern const size_t BOUNDS_N;

// Returns nullptr if the key is not a settable value.
const Bound* bounds_find(const char* key);

// Clamp v into the legal range for key. cur_minofftime supplies the dynamic
// floor for "maxrun", which must stay strictly above minofftime. Unknown keys
// pass through unchanged.
int bounds_clamp(const char* key, int v, int cur_minofftime);
```

- [ ] **Step 4: Write the implementation**

Create `shared/model/bounds.cpp`:

```cpp
#include "bounds.h"
#include <cstring>

// Mirrors the cooler's RULES.md Variables table. Two deliberate divergences,
// both documented in the design spec §4:
//   - dutypercent floors at 5 not 1 (a step-5 stepper from 1 is nonsense, and
//     sub-5 duty is moot once minruntime forces a floor on the actual run)
//   - screentimeout/sampleinterval use preset chips, not steppers
const Bound BOUNDS[] = {
    {"coolerset",      "Set point",    "C",    2,   40,  1, Widget::Stepper},
    {"range",          "Range",        "C",    0,    5,  1, Widget::Stepper},
    {"maxrun",         "Max run",      "min",  1,   60,  1, Widget::Stepper},
    {"minofftime",     "Min off",      "min",  0,   30,  1, Widget::Stepper},
    {"dutypercent",    "Duty",         "%",    5,  100,  5, Widget::Stepper},
    {"minruntime",     "Min run",      "s",    0,  600, 30, Widget::Stepper},
    {"screentimeout",  "LCD timeout",  "s",    0, 3600,  0, Widget::Preset},
    {"sampleinterval", "Sample every", "s",   10, 3600,  0, Widget::Preset},
};
const size_t BOUNDS_N = sizeof(BOUNDS) / sizeof(BOUNDS[0]);

const Bound* bounds_find(const char* key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < BOUNDS_N; i++)
        if (std::strcmp(BOUNDS[i].key, key) == 0) return &BOUNDS[i];
    return nullptr;
}

int bounds_clamp(const char* key, int v, int cur_minofftime) {
    const Bound* b = bounds_find(key);
    if (!b) return v;
    int lo = b->lo;
    if (std::strcmp(key, "maxrun") == 0) {
        int dynamic_floor = cur_minofftime + 1;
        if (dynamic_floor > lo) lo = dynamic_floor;
    }
    if (v < lo) return lo;
    if (v > b->hi) return b->hi;
    return v;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add shared/model/bounds.h shared/model/bounds.cpp tests/test_bounds.cpp CMakeLists.txt
git commit -m "feat: bounds table as single source of truth for value limits"
```

---

### Task 4: CoolerState and MQTT router

**Files:**
- Create: `shared/model/cooler_state.h`, `shared/model/cooler_state.cpp`, `shared/model/mqtt_router.h`, `shared/model/mqtt_router.cpp`, `tests/test_state.cpp`
- Modify: `CMakeLists.txt`, `shared/app.cpp`

**Interfaces:**
- Consumes: nothing
- Produces: `struct CoolerState`, `cooler_parse_data(CoolerState&, const char*, size_t, int64_t) -> bool`, `route_message(CoolerState&, const char* topic, const char* payload, size_t, int64_t) -> bool`, `router_set_prefix(const char*)`, `cooler_state()` accessor in `app.h`

- [ ] **Step 1: Write the failing test**

Create `tests/test_state.cpp`:

```cpp
#include <doctest/doctest.h>
#include "cooler_state.h"
#include "mqtt_router.h"
#include <string>
#include <fstream>
#include <sstream>

static std::string load(const char* name) {
    std::ifstream f(std::string(FIXture_DIR) + "/" + name);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

TEST_CASE("parses a live /data payload") {
    CoolerState s;
    auto j = load("data_normal.json");
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), 1785795800));
    CHECK(s.valid);
    CHECK(s.coolerset == 4);
    CHECK(s.dutypercent == 80);
    CHECK(s.minofftime == 5);
    CHECK(s.minruntime == 180);
    CHECK(s.temp == doctest::Approx(15.523));
    CHECK(s.humidity == doctest::Approx(66.35233));
    CHECK(std::string(s.mode) == "normal");
    CHECK(std::string(s.ac1) == "On");
    CHECK(std::string(s.ac2) == "Off");
    CHECK(s.ac1_relay == true);
    CHECK(s.ac2_relay == false);
    CHECK(s.lead == 1);
    CHECK(s.active_unit == 1);
    CHECK(s.cool_call == 1);
    CHECK(s.sht_fault == 0);
    CHECK(s.last_rx_epoch == 1785795800);
}

TEST_CASE("sampleinterval is read from the hist_interval_s key") {
    // The cooler ACCEPTS "sampleinterval" on /cmd but PUBLISHES the same value
    // as "hist_interval_s" on /data. Parsing must bridge the two names.
    CoolerState s;
    auto j = load("data_normal.json");
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), 0));
    CHECK(s.sampleinterval == 3600);
}

TEST_CASE("window timings parse") {
    CoolerState s;
    auto j = load("data_normal.json");
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), 0));
    CHECK(s.window_s == 900);
    CHECK(s.window_pos_s == 671);
    CHECK(s.window_on_s == 720);
    CHECK(s.swap_in_s == 229);
    CHECK(s.ac1_runtime_s == 1871u);
    CHECK(s.ac2_runtime_s == 1627u);
}

TEST_CASE("override fixture reports override mode") {
    CoolerState s;
    auto j = load("data_override.json");
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), 0));
    CHECK(std::string(s.mode) == "override");
    CHECK(s.override_on == 1);
}

TEST_CASE("fault fixture reports the sensor fault") {
    CoolerState s;
    auto j = load("data_fault.json");
    REQUIRE(cooler_parse_data(s, j.c_str(), j.size(), 0));
    CHECK(s.sht_fault == 1);
    CHECK(std::string(s.mode) == "sht30-failsafe");
}

TEST_CASE("malformed payload is rejected and leaves prior state intact") {
    CoolerState s;
    auto good = load("data_normal.json");
    REQUIRE(cooler_parse_data(s, good.c_str(), good.size(), 100));
    auto bad = load("data_malformed.json");
    CHECK_FALSE(cooler_parse_data(s, bad.c_str(), bad.size(), 200));
    CHECK(s.coolerset == 4);          // unchanged
    CHECK(s.last_rx_epoch == 100);    // not advanced by the bad message
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

TEST_CASE("router ignores unrelated and per-unit topics") {
    router_set_prefix("ha/esp32-cooler");
    CoolerState s;
    CHECK_FALSE(route_message(s, "ha/esp32-cooler/ac1", "{}", 2, 0));
    CHECK_FALSE(route_message(s, "ha/other/data", "{}", 2, 0));
    CHECK_FALSE(route_message(s, "totally/unrelated", "{}", 2, 0));
}
```

- [ ] **Step 2: Add the test to the build and run it to verify it fails**

Add `tests/test_state.cpp` to `cooler_tests` in `CMakeLists.txt`.

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL — `cooler_state.h: No such file or directory`

- [ ] **Step 3: Write cooler_state.h**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

struct CoolerState {
    // ---- settable config ----
    int coolerset = 0;
    int range = 0;
    int maxrun = 0;          // minutes
    int minofftime = 0;      // minutes
    int dutypercent = 0;
    int minruntime = 0;      // seconds
    int screentimeout = 0;   // seconds
    int sampleinterval = 0;  // seconds -- published as "hist_interval_s"

    // ---- live ----
    float temp = 0.0f;
    float humidity = 0.0f;
    int cool_call = 0;
    int override_on = 0;
    int sht_fault = 0;
    int lead = 0;            // 1 or 2
    int active_unit = 0;     // 0 none, 1, 2
    int pot_pct = 0;
    bool ac_on = false;
    bool in_duty = false;
    char mode[20] = {0};     // "normal" | "override" | "sht30-failsafe"

    // ---- per unit ----
    char ac1[8] = {0};       // "On" | "Duty" | "Off"
    char ac2[8] = {0};
    bool ac1_relay = false;
    bool ac2_relay = false;
    int ac1_blocked = 0;
    int ac2_blocked = 0;
    uint32_t ac1_runtime_s = 0;
    uint32_t ac2_runtime_s = 0;

    // ---- window ----
    int window_s = 0;
    int window_pos_s = 0;
    int window_on_s = 0;
    int swap_in_s = 0;

    // ---- misc ----
    uint32_t uptime_s = 0;
    int64_t date = 0;        // cooler's own epoch

    // ---- panel-side meta ----
    int64_t last_rx_epoch = 0;
    bool valid = false;
    bool online = false;     // from the availability topic
};

// Parse a /data payload. Returns false and leaves s untouched on malformed
// input. now_epoch stamps last_rx_epoch.
bool cooler_parse_data(CoolerState& s, const char* json, size_t len, int64_t now_epoch);
```

- [ ] **Step 4: Write cooler_state.cpp**

```cpp
#include "cooler_state.h"
#include <ArduinoJson.h>
#include <cstring>

static void copy_str(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = 0; return; }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

bool cooler_parse_data(CoolerState& s, const char* json, size_t len, int64_t now_epoch) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    if (!doc["temp"].is<float>() && !doc["temp"].is<int>()) return false;

    s.coolerset      = doc["coolerset"]      | s.coolerset;
    s.range          = doc["range"]          | s.range;
    s.maxrun         = doc["maxrun"]         | s.maxrun;
    s.minofftime     = doc["minofftime"]     | s.minofftime;
    s.dutypercent    = doc["dutypercent"]    | s.dutypercent;
    s.minruntime     = doc["minruntime"]     | s.minruntime;
    s.screentimeout  = doc["screentimeout"]  | s.screentimeout;
    // NOTE: set as "sampleinterval", published as "hist_interval_s".
    s.sampleinterval = doc["hist_interval_s"] | s.sampleinterval;

    s.temp        = doc["temp"]        | s.temp;
    s.humidity    = doc["humidity"]    | s.humidity;
    s.cool_call   = doc["cool_call"]   | s.cool_call;
    s.override_on = doc["override"]    | s.override_on;
    s.sht_fault   = doc["sht_fault"]   | s.sht_fault;
    s.lead        = doc["lead"]        | s.lead;
    s.active_unit = doc["active_unit"] | s.active_unit;
    s.pot_pct     = doc["pot_pct"]     | s.pot_pct;
    s.ac_on       = doc["ac_on"]       | s.ac_on;
    s.in_duty     = doc["in_duty"]     | s.in_duty;
    copy_str(s.mode, sizeof(s.mode), doc["mode"] | "");
    copy_str(s.ac1,  sizeof(s.ac1),  doc["ac1"]  | "");
    copy_str(s.ac2,  sizeof(s.ac2),  doc["ac2"]  | "");

    s.ac1_relay     = doc["ac1_relay"]     | s.ac1_relay;
    s.ac2_relay     = doc["ac2_relay"]     | s.ac2_relay;
    s.ac1_blocked   = doc["ac1_blocked"]   | s.ac1_blocked;
    s.ac2_blocked   = doc["ac2_blocked"]   | s.ac2_blocked;
    s.ac1_runtime_s = doc["ac1_runtime_s"] | s.ac1_runtime_s;
    s.ac2_runtime_s = doc["ac2_runtime_s"] | s.ac2_runtime_s;

    s.window_s     = doc["window_s"]     | s.window_s;
    s.window_pos_s = doc["window_pos_s"] | s.window_pos_s;
    s.window_on_s  = doc["window_on_s"]  | s.window_on_s;
    s.swap_in_s    = doc["swap_in_s"]    | s.swap_in_s;

    s.uptime_s = doc["uptime_s"] | s.uptime_s;
    s.date     = doc["date"]     | s.date;

    s.last_rx_epoch = now_epoch;
    s.valid = true;
    return true;
}
```

- [ ] **Step 5: Write mqtt_router.h**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
struct CoolerState;

// Topic tree root the router matches against. Default "ha/esp32-cooler".
void router_set_prefix(const char* prefix);
const char* router_prefix(void);

// Returns true if the message was recognised and applied.
bool route_message(CoolerState& s, const char* topic, const char* payload,
                   size_t len, int64_t now_epoch);
```

- [ ] **Step 6: Write mqtt_router.cpp**

```cpp
#include "mqtt_router.h"
#include "cooler_state.h"
#include <cstring>

static char s_prefix[64] = "ha/esp32-cooler";

void router_set_prefix(const char* prefix) {
    if (!prefix) return;
    std::strncpy(s_prefix, prefix, sizeof(s_prefix) - 1);
    s_prefix[sizeof(s_prefix) - 1] = 0;
}
const char* router_prefix(void) { return s_prefix; }

bool route_message(CoolerState& s, const char* topic, const char* payload,
                   size_t len, int64_t now_epoch) {
    if (!topic || !payload) return false;
    size_t pn = std::strlen(s_prefix);
    if (std::strncmp(topic, s_prefix, pn) != 0) return false;
    if (topic[pn] != '/') return false;
    const char* leaf = topic + pn + 1;

    if (std::strcmp(leaf, "data") == 0)
        return cooler_parse_data(s, payload, len, now_epoch);

    if (std::strcmp(leaf, "availability") == 0) {
        s.online = (len == 6 && std::strncmp(payload, "online", 6) == 0);
        return true;
    }
    // /ac1 and /ac2 are deliberately not handled -- every field they carry
    // already exists in /data, and two sources for one fact invites drift.
    return false;
}
```

- [ ] **Step 7: Wire the state into app.cpp**

In `shared/app.h`, add before the `extern "C"` block:

```cpp
struct CoolerState;
CoolerState& cooler_state();
```

In `shared/app.cpp`, add:

```cpp
#include "cooler_state.h"
#include "mqtt_router.h"

static CoolerState g_state;
CoolerState& cooler_state() { return g_state; }

void app_on_mqtt_message(const char* topic, const uint8_t* payload, size_t len) {
    route_message(g_state, topic, (const char*)payload, len, platform_epoch_utc());
}
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS, including `portability_guard`.

- [ ] **Step 9: Commit**

```bash
git add shared/model/cooler_state.* shared/model/mqtt_router.* shared/app.* \
        tests/test_state.cpp CMakeLists.txt
git commit -m "feat: CoolerState parsing and MQTT router"
```

---

# Phase B — History and trend screen

### Task 5: History ring buffer

**Files:**
- Create: `shared/model/history.h`, `shared/model/history.cpp`, `tests/test_history.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing
- Produces: `struct Sample {int64_t t; int16_t temp_c10; int16_t rh_c10;}`, `class History` with `init(size_t)`, `clear()`, `maybe_append(int64_t,float,float,int)`, `size()`, `capacity()`, `at(size_t)`, `newest_epoch()`, `oldest_epoch()`

- [ ] **Step 1: Write the failing test**

Create `tests/test_history.cpp`:

```cpp
#include <doctest/doctest.h>
#include "history.h"

TEST_CASE("append stores full precision") {
    History h;
    REQUIRE(h.init(10));
    CHECK(h.maybe_append(1000, 15.52f, 66.35f, 30));
    REQUIRE(h.size() == 1);
    CHECK(h.at(0).temp_c10 == 155);
    CHECK(h.at(0).rh_c10 == 664);   // 66.35 -> 663.5 rounds to 664
    CHECK(h.at(0).t == 1000);
}

TEST_CASE("time gate rejects samples closer than min_gap_s") {
    History h;
    REQUIRE(h.init(10));
    CHECK(h.maybe_append(1000, 10.0f, 50.0f, 30));
    CHECK_FALSE(h.maybe_append(1010, 11.0f, 51.0f, 30));  // only 10s later
    CHECK_FALSE(h.maybe_append(1029, 11.0f, 51.0f, 30));  // 29s
    CHECK(h.maybe_append(1030, 12.0f, 52.0f, 30));        // exactly 30s
    CHECK(h.size() == 2);
    CHECK(h.at(1).temp_c10 == 120);
}

TEST_CASE("ring wraps and drops oldest") {
    History h;
    REQUIRE(h.init(3));
    h.maybe_append(100, 1.0f, 10.0f, 30);
    h.maybe_append(200, 2.0f, 20.0f, 30);
    h.maybe_append(300, 3.0f, 30.0f, 30);
    CHECK(h.size() == 3);
    h.maybe_append(400, 4.0f, 40.0f, 30);
    CHECK(h.size() == 3);
    CHECK(h.at(0).t == 200);       // 100 dropped
    CHECK(h.at(2).t == 400);
    CHECK(h.oldest_epoch() == 200);
    CHECK(h.newest_epoch() == 400);
}

TEST_CASE("negative temperatures survive the round trip") {
    History h;
    REQUIRE(h.init(4));
    h.maybe_append(100, -3.4f, 80.0f, 30);
    CHECK(h.at(0).temp_c10 == -34);
}

TEST_CASE("clear empties without freeing") {
    History h;
    REQUIRE(h.init(4));
    h.maybe_append(100, 1.0f, 10.0f, 30);
    h.clear();
    CHECK(h.size() == 0);
    CHECK(h.capacity() == 4);
    CHECK(h.maybe_append(101, 2.0f, 20.0f, 30));   // gate resets too
}

TEST_CASE("init(0) fails cleanly") {
    History h;
    CHECK_FALSE(h.init(0));
    CHECK(h.size() == 0);
}
```

- [ ] **Step 2: Add to build and run to verify failure**

Add `tests/test_history.cpp` to `cooler_tests`.
Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL — `history.h: No such file or directory`

- [ ] **Step 3: Write history.h**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

// 16 bytes with padding. 7 days at 30 s spacing = 20160 samples = 322 KB,
// comfortable in the S3's 8 MB PSRAM. Per-sample epoch (rather than assuming
// even spacing) keeps the time axis correct across MQTT dropouts.
struct Sample {
    int64_t t;          // epoch seconds
    int16_t temp_c10;   // degrees C x10
    int16_t rh_c10;     // %RH x10
};

class History {
public:
    ~History();
    bool init(size_t capacity);   // false on cap==0 or allocation failure
    void clear();

    // Store only if at least min_gap_s has elapsed since the last stored
    // sample. /data publishes on change as well as every 30 s, so without
    // this gate a burst of relay activity over-samples that period.
    bool maybe_append(int64_t epoch, float temp, float rh, int min_gap_s);

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
```

- [ ] **Step 4: Write history.cpp**

```cpp
#include "history.h"
#include <cstdlib>
#include <cmath>

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

bool History::maybe_append(int64_t epoch, float temp, float rh, int min_gap_s) {
    if (!buf_) return false;
    if (count_ > 0 && (epoch - newest_epoch()) < (int64_t)min_gap_s) return false;
    Sample s{epoch, to_c10(temp), to_c10(rh)};
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
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add shared/model/history.* tests/test_history.cpp CMakeLists.txt
git commit -m "feat: PSRAM history ring buffer with time-gated sampling"
```

---

### Task 6: Chart downsampling

**Files:**
- Create: `shared/ui/chart.h`, `shared/ui/chart.cpp`, `tests/test_chart.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `History`, `Sample` (Task 5)
- Produces: `struct Column {float tmin,tmax,hmin,hmax; bool has;}`, `chart_downsample(const History&, int64_t from, int64_t to, Column* out, size_t ncols)`

- [ ] **Step 1: Write the failing test**

Create `tests/test_chart.cpp`:

```cpp
#include <doctest/doctest.h>
#include "chart.h"
#include "history.h"

TEST_CASE("downsample buckets samples into columns") {
    History h;
    REQUIRE(h.init(100));
    // 10 samples, t = 0,10,...,90
    for (int i = 0; i < 10; i++)
        h.maybe_append(i * 10, (float)i, (float)(i * 2), 0);

    Column cols[5];
    chart_downsample(h, 0, 100, cols, 5);   // each column spans 20 s
    for (int i = 0; i < 5; i++) CHECK(cols[i].has);
    // column 0 covers t in [0,20) -> samples 0 and 1 -> temps 0.0 and 1.0
    CHECK(cols[0].tmin == doctest::Approx(0.0));
    CHECK(cols[0].tmax == doctest::Approx(1.0));
    // column 4 covers [80,100) -> samples 8 and 9 -> temps 8.0 and 9.0
    CHECK(cols[4].tmin == doctest::Approx(8.0));
    CHECK(cols[4].tmax == doctest::Approx(9.0));
    CHECK(cols[4].hmax == doctest::Approx(18.0));
}

TEST_CASE("empty columns are flagged not guessed") {
    History h;
    REQUIRE(h.init(10));
    h.maybe_append(0, 5.0f, 50.0f, 0);
    h.maybe_append(90, 6.0f, 60.0f, 0);
    Column cols[5];
    chart_downsample(h, 0, 100, cols, 5);
    CHECK(cols[0].has);
    CHECK_FALSE(cols[1].has);   // gap -- must NOT interpolate
    CHECK_FALSE(cols[2].has);
    CHECK_FALSE(cols[3].has);
    CHECK(cols[4].has);
}

TEST_CASE("empty history yields all-empty columns") {
    History h;
    REQUIRE(h.init(10));
    Column cols[3];
    chart_downsample(h, 0, 100, cols, 3);
    for (int i = 0; i < 3; i++) CHECK_FALSE(cols[i].has);
}

TEST_CASE("samples outside the window are excluded") {
    History h;
    REQUIRE(h.init(10));
    h.maybe_append(10,  1.0f, 10.0f, 0);
    h.maybe_append(500, 9.0f, 90.0f, 0);   // outside [0,100)
    Column cols[2];
    chart_downsample(h, 0, 100, cols, 2);
    CHECK(cols[0].has);
    CHECK(cols[0].tmax == doctest::Approx(1.0));
    CHECK_FALSE(cols[1].has);
}
```

- [ ] **Step 2: Add to build and run to verify failure**

Add `tests/test_chart.cpp` to `cooler_tests`, and add `shared/ui` is already in `target_include_directories`.

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL — `chart.h: No such file or directory`

- [ ] **Step 3: Write chart.h**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
class History;

// One horizontal pixel column of the trend. min/max preserve spikes that a
// plain average would erase. `has` is false when no sample fell in the
// column -- render a gap, never interpolate across a data outage.
struct Column {
    float tmin, tmax;
    float hmin, hmax;
    bool has;
};

// Bucket history samples in [from, to) into ncols evenly spaced columns.
void chart_downsample(const History& h, int64_t from, int64_t to,
                      Column* out, size_t ncols);
```

- [ ] **Step 4: Write chart.cpp**

```cpp
#include "chart.h"
#include "history.h"

void chart_downsample(const History& h, int64_t from, int64_t to,
                      Column* out, size_t ncols) {
    for (size_t i = 0; i < ncols; i++)
        out[i] = Column{0, 0, 0, 0, false};
    if (ncols == 0 || to <= from) return;

    const int64_t span = to - from;
    for (size_t i = 0; i < h.size(); i++) {
        const Sample& s = h.at(i);
        if (s.t < from || s.t >= to) continue;
        size_t c = (size_t)(((s.t - from) * (int64_t)ncols) / span);
        if (c >= ncols) c = ncols - 1;
        const float t = s.temp_c10 / 10.0f;
        const float rh = s.rh_c10 / 10.0f;
        if (!out[c].has) {
            out[c] = Column{t, t, rh, rh, true};
        } else {
            if (t  < out[c].tmin) out[c].tmin = t;
            if (t  > out[c].tmax) out[c].tmax = t;
            if (rh < out[c].hmin) out[c].hmin = rh;
            if (rh > out[c].hmax) out[c].hmax = rh;
        }
    }
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add shared/ui/chart.* tests/test_chart.cpp CMakeLists.txt
git commit -m "feat: chart downsampling with min/max columns and honest gaps"
```

---

### Task 7: Trend screen

**Files:**
- Create: `shared/ui/screen_trend.h`, `shared/ui/screen_trend.cpp`
- Modify: `shared/ui/ui.cpp`, `shared/app.cpp`, `sim/mqtt_mosq.cpp`

**Interfaces:**
- Consumes: `cooler_state()` (Task 4), `History` (Task 5), `chart_downsample` (Task 6)
- Produces: `screen_trend_create(lv_obj_t* parent) -> lv_obj_t*`, `screen_trend_refresh()`, `panel_history() -> History&`, `trend_set_zoom(int seconds)`

- [ ] **Step 1: Add the history accessor and sampling hook**

In `shared/app.h` add:

```cpp
class History;
History& panel_history();
```

In `shared/app.cpp`:

```cpp
#include "history.h"

static History g_hist;
History& panel_history() { return g_hist; }

// 7 days at 30 s. Falls back to 24 h if the big allocation fails.
void app_init_history() {
    if (!g_hist.init(20160)) g_hist.init(2880);
}
```

Extend `app_on_mqtt_message` so a successful `/data` route also samples:

```cpp
void app_on_mqtt_message(const char* topic, const uint8_t* payload, size_t len) {
    if (!route_message(g_state, topic, (const char*)payload, len, platform_epoch_utc()))
        return;
    if (g_state.valid)
        g_hist.maybe_append(platform_epoch_utc(), g_state.temp, g_state.humidity, 30);
}
```

- [ ] **Step 2: Write screen_trend.h**

```cpp
#pragma once
#include "lvgl.h"

lv_obj_t* screen_trend_create(lv_obj_t* parent);
void screen_trend_refresh(void);
void trend_set_zoom(int seconds);   // 3600 | 86400 | 604800
int  trend_zoom(void);
```

- [ ] **Step 3: Write screen_trend.cpp**

```cpp
#include "screen_trend.h"
#include "chart.h"
#include "history.h"
#include "cooler_state.h"
#include "app.h"
#include "theme.h"
#include <cstdio>

#define GRAPH_X 8
#define GRAPH_Y 72
#define GRAPH_W 464
#define GRAPH_H 228
#define NCOLS   116          // GRAPH_W / 4 -- one column per 4 px

static lv_obj_t* s_root;
static lv_obj_t* s_temp_lbl;
static lv_obj_t* s_set_lbl;
static lv_obj_t* s_canvas;
static lv_obj_t* s_ac1_lbl;
static lv_obj_t* s_ac2_lbl;
static lv_obj_t* s_foot_lbl;
static lv_color_t* s_cbuf;
static int s_zoom = 3600;

void trend_set_zoom(int seconds) { s_zoom = seconds; screen_trend_refresh(); }
int  trend_zoom(void) { return s_zoom; }

lv_obj_t* screen_trend_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_temp_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_temp_lbl, 8, 8);
    lv_obj_set_style_text_font(s_temp_lbl, &lv_font_montserrat_48, 0);

    s_set_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_set_lbl, 260, 24);

    s_cbuf = (lv_color_t*)lv_malloc(
        LV_CANVAS_BUF_SIZE(GRAPH_W, GRAPH_H, 16, LV_DRAW_BUF_STRIDE_ALIGN));
    s_canvas = lv_canvas_create(s_root);
    lv_canvas_set_buffer(s_canvas, s_cbuf, GRAPH_W, GRAPH_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, GRAPH_X, GRAPH_Y);

    static const char* zooms[] = {"1h", "24h", "7d"};
    static const int zsec[] = {3600, 86400, 604800};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = lv_button_create(s_root);
        lv_obj_set_size(b, 100, 34);
        lv_obj_set_pos(b, 60 + i * 120, 302);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, zooms[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            trend_set_zoom((int)(intptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, (void*)(intptr_t)zsec[i]);
    }

    s_ac1_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_ac1_lbl, 20, 352);
    s_ac2_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_ac2_lbl, 260, 352);
    s_foot_lbl = lv_label_create(s_root);
    lv_obj_set_pos(s_foot_lbl, 20, 442);

    screen_trend_refresh();
    return s_root;
}

static void draw_graph(void) {
    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);
    lv_canvas_fill_bg(s_canvas, lv_color_hex(0x101418), LV_OPA_COVER);

    const CoolerState& st = cooler_state();
    History& h = panel_history();
    if (h.size() < 2) { lv_canvas_finish_layer(s_canvas, &layer); return; }

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
    if (lo > hi) { lv_canvas_finish_layer(s_canvas, &layer); return; }
    const float band_lo = (float)(st.coolerset - st.range);
    const float band_hi = (float)(st.coolerset + st.range);
    if (band_lo < lo) lo = band_lo;
    if (band_hi > hi) hi = band_hi;
    if (hi - lo < 1.0f) { hi = lo + 1.0f; }
    const float pad = (hi - lo) * 0.1f;
    lo -= pad; hi += pad;

    auto y_of = [&](float v) -> int32_t {
        return (int32_t)(GRAPH_H - 1 - ((v - lo) / (hi - lo)) * (GRAPH_H - 1));
    };

    // Setpoint band
    lv_draw_rect_dsc_t band;
    lv_draw_rect_dsc_init(&band);
    band.bg_color = lv_color_hex(0x1d3a2a);
    band.bg_opa = LV_OPA_60;
    lv_area_t ba = {0, y_of(band_hi), GRAPH_W - 1, y_of(band_lo)};
    lv_draw_rect(&layer, &band, &ba);

    // Temperature min/max bars
    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.bg_color = lv_color_hex(0x4ea3ff);
    bar.bg_opa = LV_OPA_COVER;
    const int32_t colw = GRAPH_W / NCOLS;
    for (int i = 0; i < NCOLS; i++) {
        if (!cols[i].has) continue;      // gap stays blank
        int32_t y0 = y_of(cols[i].tmax);
        int32_t y1 = y_of(cols[i].tmin);
        if (y1 - y0 < 2) y1 = y0 + 2;    // keep flat runs visible
        lv_area_t a = {i * colw, y0, i * colw + colw - 1, y1};
        lv_draw_rect(&layer, &bar, &a);
    }
    lv_canvas_finish_layer(s_canvas, &layer);
}

void screen_trend_refresh(void) {
    if (!s_root) return;
    const CoolerState& st = cooler_state();
    char buf[64];

    if (st.valid) snprintf(buf, sizeof(buf), "%.1f", (double)st.temp);
    else          snprintf(buf, sizeof(buf), "--.-");
    lv_label_set_text(s_temp_lbl, buf);

    snprintf(buf, sizeof(buf), "set %d  +-%d", st.coolerset, st.range);
    lv_label_set_text(s_set_lbl, buf);

    snprintf(buf, sizeof(buf), "AC1  %s%s", st.ac1_relay ? "ON" : "--",
             st.lead == 1 ? "   LEAD" : "");
    lv_label_set_text(s_ac1_lbl, buf);
    snprintf(buf, sizeof(buf), "AC2  %s%s", st.ac2_relay ? "ON" : "--",
             st.lead == 2 ? "   LEAD" : "");
    lv_label_set_text(s_ac2_lbl, buf);

    snprintf(buf, sizeof(buf), "duty %d%%   swap %dm   RH %.0f%%",
             st.dutypercent, (st.swap_in_s + 59) / 60, (double)st.humidity);
    lv_label_set_text(s_foot_lbl, buf);

    draw_graph();
}
```

- [ ] **Step 4: Wire into ui.cpp**

In `shared/ui/ui.cpp`, replace the LLMMon dashboard creation with `screen_trend_create(lv_screen_active())`, and make the periodic refresh call `screen_trend_refresh()`. Call `app_init_history()` during `ui_init()`.

- [ ] **Step 5: Point the simulator at the cooler's topic tree**

In `sim/mqtt_mosq.cpp`, change the subscribe in `on_connect` and the client id:

```cpp
mosquitto_subscribe(m, nullptr, "ha/esp32-cooler/#", 1);
```
```cpp
struct mosquitto* m = mosquitto_new("cooler-panel-sim", true, nullptr);
```

- [ ] **Step 6: Build and run against the live broker**

Run:
```bash
cmake --build build -j
./build/cooler_sim --host mqtt.example.com --port 1883 \
    --no-tls --user <user> --pass <REDACTED - see tools/import_cooler_broker.sh>
```
Expected: window shows the live cooler temperature, setpoint, AC1/AC2 state, and a graph that fills in over the next few minutes as samples accumulate.

- [ ] **Step 7: Run the test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS including `portability_guard` (screen_trend.cpp must not reference SDL or mosquitto).

- [ ] **Step 8: Commit**

```bash
git add shared/ui/screen_trend.* shared/ui/ui.cpp shared/app.* sim/mqtt_mosq.cpp
git commit -m "feat: trend screen with live graph and zoom levels"
```

---

# Phase C — Command path

> **Risk note:** this phase writes to the live cooler for the first time. Step through Task 9's manual verification using `screentimeout` only — it changes nothing about refrigeration. Do not exercise `coolerset` or `dutypercent` until the reconciliation tests pass.

### Task 8: Outbound publish through the platform seam

**Files:**
- Modify: `shared/platform.h`, `sim/mqtt_mosq.cpp`, `device/src/mqtt_pubsub.cpp`, `device/src/mqtt_pubsub.h`
- Create: `tests/test_publish_stub.cpp`

**Interfaces:**
- Consumes: nothing
- Produces: `platform_mqtt_publish(const char* topic, const char* payload, size_t len, bool retain) -> bool`

- [ ] **Step 1: Extend the platform seam**

In `shared/platform.h`, inside the `extern "C"` block:

```cpp
/* Publish to the broker. Returns false if the link is down or the payload
   was rejected. Implemented per-platform: libmosquitto in sim/, PubSubClient
   on device/. Tests link a stub. */
bool platform_mqtt_publish(const char* topic, const char* payload,
                           size_t len, bool retain);
```

- [ ] **Step 2: Write the test stub so the unit tests can link and assert**

Create `tests/test_publish_stub.h`:

```cpp
#pragma once
#include <string>
#include <vector>

struct PubRecord { std::string topic, payload; bool retain; };

// Everything published since the last reset, in order.
std::vector<PubRecord>& test_publishes();
// Clear the log. fail=true makes subsequent publishes return false.
void test_publish_reset(bool fail);
```

Create `tests/test_publish_stub.cpp`:

```cpp
// Test-only implementation of the publish seam. Records every publish so
// command tests can assert on exactly what would have gone to the broker.
#include "test_publish_stub.h"
#include "platform.h"

static std::vector<PubRecord> g_pubs;
static bool g_fail = false;

std::vector<PubRecord>& test_publishes() { return g_pubs; }
void test_publish_reset(bool fail) { g_pubs.clear(); g_fail = fail; }

extern "C" bool platform_mqtt_publish(const char* topic, const char* payload,
                                      size_t len, bool retain) {
    if (g_fail) return false;
    g_pubs.push_back({std::string(topic), std::string(payload, len), retain});
    return true;
}
```

`PubRecord` is defined once, in the header. Add `tests/` to the test target's include directories in `CMakeLists.txt` so `test_commands.cpp` can `#include "test_publish_stub.h"`:

```cmake
target_include_directories(cooler_tests PRIVATE tests)
```

- [ ] **Step 3: Implement in the simulator**

Append to `sim/mqtt_mosq.cpp`:

```cpp
extern "C" bool platform_mqtt_publish(const char* topic, const char* payload,
                                      size_t len, bool retain) {
    if (!g_mosq) return false;
    int rc = mosquitto_publish(g_mosq, nullptr, topic, (int)len, payload, 1, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mqtt: publish to %s failed rc=%d\n", topic, rc);
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Implement on the device**

Append to `device/src/mqtt_pubsub.cpp`:

```cpp
extern "C" bool platform_mqtt_publish(const char* topic, const char* payload,
                                      size_t len, bool retain) {
    if (!s_mqtt.connected()) return false;
    return s_mqtt.publish(topic, (const uint8_t*)payload, (unsigned int)len, retain);
}
```

- [ ] **Step 5: Add the stub to the test target and build**

Add `tests/test_publish_stub.cpp` to `cooler_tests` in `CMakeLists.txt`.

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: builds and all existing tests still PASS.

- [ ] **Step 6: Commit**

```bash
git add shared/platform.h sim/mqtt_mosq.cpp device/src/mqtt_pubsub.cpp \
        tests/test_publish_stub.* CMakeLists.txt
git commit -m "feat: add outbound publish to the platform seam"
```

---

### Task 9: Commands with debounce and reconciliation

**Files:**
- Create: `shared/model/commands.h`, `shared/model/commands.cpp`, `tests/test_commands.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `bounds_clamp` (Task 3), `CoolerState` (Task 4), `platform_mqtt_publish` (Task 8), `router_prefix()` (Task 4)
- Produces: `class Commands` with `request(const char* key, int value, uint32_t now_ms)`, `tick(uint32_t now_ms)`, `on_state(const CoolerState&)`, `is_pending(const char*)`, `display_value(const char*, const CoolerState&)`, `take_toast(char*, size_t)`; constants `CMD_DEBOUNCE_MS = 400`, `CMD_DEADLINE_MS = 3000`

- [ ] **Step 1: Write the failing test**

Create `tests/test_commands.cpp`:

```cpp
#include <doctest/doctest.h>
#include "commands.h"
#include "cooler_state.h"
#include "mqtt_router.h"
#include "test_publish_stub.h"
#include <cstring>

static CoolerState base_state() {
    CoolerState s;
    s.valid = true;
    s.coolerset = 4; s.range = 2; s.maxrun = 15;
    s.minofftime = 5; s.dutypercent = 80; s.minruntime = 180;
    s.screentimeout = 300; s.sampleinterval = 3600;
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

TEST_CASE("maxrun clamp uses the live minofftime") {
    test_publish_reset(false);
    Commands c;
    CoolerState s = base_state();
    s.minofftime = 12;
    c.on_state(s);
    c.request("maxrun", 3, 1000);
    c.tick(1500);
    REQUIRE(test_publishes().size() == 1);
    CHECK(test_publishes()[0].payload == "{\"maxrun\":13}");
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
```

- [ ] **Step 2: Add to build and run to verify failure**

Add `tests/test_commands.cpp` to `cooler_tests`.
Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL — `commands.h: No such file or directory`

- [ ] **Step 3: Write commands.h**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "cooler_state.h"

static const uint32_t CMD_DEBOUNCE_MS = 400;
static const uint32_t CMD_DEADLINE_MS = 3000;
static const size_t   CMD_MAX_PENDING = 8;

class Commands {
public:
    // Queue a change. Clamped against the bounds table using the last known
    // minofftime. Repeated calls for the same key inside the debounce window
    // coalesce into one publish of the final value.
    void request(const char* key, int value, uint32_t now_ms);

    // Drive debounce flushes and deadline expiry. Call every UI tick.
    void tick(uint32_t now_ms);

    // Feed every fresh /data. Clears pending entries the cooler has answered.
    void on_state(const CoolerState& s);

    bool is_pending(const char* key) const;

    // What the UI should show: the optimistic requested value while pending,
    // otherwise the authoritative value from state.
    int display_value(const char* key, const CoolerState& s) const;

    // One-shot failure notice. Returns false when there is nothing to show.
    bool take_toast(char* out, size_t n);

private:
    struct Entry {
        char key[20] = {0};
        int  want = 0;
        int  pre = 0;          // value at publish time, for the clear rule
        uint32_t queued_ms = 0;
        uint32_t sent_ms = 0;
        bool active = false;
        bool sent = false;
    };
    Entry entries_[CMD_MAX_PENDING];
    CoolerState last_;         // most recent /data, for pre-values
    int last_minofftime_ = 0;
    char toast_[64] = {0};
    bool has_toast_ = false;

    Entry* find(const char* key);
    const Entry* find(const char* key) const;
    Entry* alloc(const char* key);
    void   set_toast(const char* key);
};
```

- [ ] **Step 4: Write commands.cpp**

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
    if (!std::strcmp(key, "maxrun"))         return s.maxrun;
    if (!std::strcmp(key, "minofftime"))     return s.minofftime;
    if (!std::strcmp(key, "dutypercent"))    return s.dutypercent;
    if (!std::strcmp(key, "minruntime"))     return s.minruntime;
    if (!std::strcmp(key, "screentimeout"))  return s.screentimeout;
    if (!std::strcmp(key, "sampleinterval")) return s.sampleinterval;
    if (!std::strcmp(key, "override"))       return s.override_on;
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
    if (!e) return;
    if (!e->sent) e->pre = state_value(key, last_);   // value before we touched it
    e->want = bounds_clamp(key, value, last_minofftime_);
    e->queued_ms = now_ms;
    e->sent = false;
}

void Commands::tick(uint32_t now_ms) {
    for (auto& e : entries_) {
        if (!e.active) continue;
        if (!e.sent) {
            if (now_ms - e.queued_ms < CMD_DEBOUNCE_MS) continue;
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
    last_minofftime_ = s.minofftime;
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

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS

- [ ] **Step 6: Manual verification against the live cooler — harmless value only**

Add a temporary hook in `sim/main.cpp` that fires one request on startup, then run:

```bash
./build/cooler_sim --host mqtt.example.com --port 1883 \
    --no-tls --user <user> --pass <REDACTED - see tools/import_cooler_broker.sh>
```

In another terminal, watch the round trip:

```bash
mosquitto_sub -h mqtt.example.com -p 1883 -u <user> -P <REDACTED - see tools/import_cooler_broker.sh> \
  -t 'ha/esp32-cooler/cmd' -t 'ha/esp32-cooler/data' -v
```

Expected: your `{"screentimeout":600}` appears on `/cmd`, and a `/data` echoing `"screentimeout":600` follows within a second. Restore it with `{"screentimeout":300}` and remove the temporary hook.

- [ ] **Step 7: Commit**

```bash
git add shared/model/commands.* tests/test_commands.cpp CMakeLists.txt
git commit -m "feat: command path with debounce, clamping and reconciliation"
```

---

### Task 10: Settings screen

**Files:**
- Create: `shared/ui/screen_settings.h`, `shared/ui/screen_settings.cpp`
- Modify: `shared/ui/ui.cpp`, `shared/app.cpp`, `shared/app.h`

**Interfaces:**
- Consumes: `BOUNDS`/`Widget` (Task 3), `CoolerState` (Task 4), `Commands` (Task 9)
- Produces: `screen_settings_create(lv_obj_t*) -> lv_obj_t*`, `screen_settings_refresh()`, `panel_commands() -> Commands&`

- [ ] **Step 1: Expose the Commands singleton**

In `shared/app.h`:

```cpp
class Commands;
Commands& panel_commands();
```

In `shared/app.cpp`:

```cpp
#include "commands.h"
static Commands g_cmds;
Commands& panel_commands() { return g_cmds; }
```

Call `g_cmds.on_state(g_state)` at the end of `app_on_mqtt_message` after a successful route.

- [ ] **Step 2: Write screen_settings.h**

```cpp
#pragma once
#include "lvgl.h"
lv_obj_t* screen_settings_create(lv_obj_t* parent);
void screen_settings_refresh(void);
```

- [ ] **Step 3: Write screen_settings.cpp**

```cpp
#include "screen_settings.h"
#include "bounds.h"
#include "commands.h"
#include "cooler_state.h"
#include "app.h"
#include "platform.h"
#include <cstdio>
#include <cstring>

#define ROW_H 56

static lv_obj_t* s_root;
static lv_obj_t* s_val[16];
static lv_obj_t* s_ovr_btn;

struct StepCtx { const Bound* b; int dir; };
static StepCtx s_ctx[16 * 2];

static void on_step(lv_event_t* e) {
    StepCtx* c = (StepCtx*)lv_event_get_user_data(e);
    const CoolerState& st = cooler_state();
    Commands& cmd = panel_commands();
    int cur = cmd.display_value(c->b->key, st);
    int step = c->b->step ? c->b->step : 1;
    cmd.request(c->b->key, cur + c->dir * step, platform_now_ms());
    screen_settings_refresh();
}

static void on_override(lv_event_t*) {
    const CoolerState& st = cooler_state();
    panel_commands().request("override", st.override_on ? 0 : 1, platform_now_ms());
    screen_settings_refresh();
}

lv_obj_t* screen_settings_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_pad_all(s_root, 4, 0);

    int y = 4;
    int ci = 0;
    for (size_t i = 0; i < BOUNDS_N; i++) {
        const Bound* b = &BOUNDS[i];
        if (b->widget != Widget::Stepper) continue;   // presets handled below

        lv_obj_t* lbl = lv_label_create(s_root);
        lv_label_set_text(lbl, b->label);
        lv_obj_set_pos(lbl, 8, y + 16);

        s_val[i] = lv_label_create(s_root);
        lv_obj_set_pos(s_val[i], 210, y + 16);

        for (int d = 0; d < 2; d++) {
            lv_obj_t* btn = lv_button_create(s_root);
            lv_obj_set_size(btn, 60, 48);
            lv_obj_set_pos(btn, d == 0 ? 330 : 400, y);
            lv_obj_t* t = lv_label_create(btn);
            lv_label_set_text(t, d == 0 ? "-" : "+");
            lv_obj_center(t);
            s_ctx[ci] = StepCtx{b, d == 0 ? -1 : +1};
            lv_obj_add_event_cb(btn, on_step, LV_EVENT_CLICKED, &s_ctx[ci]);
            ci++;
        }
        y += ROW_H;
    }

    s_ovr_btn = lv_button_create(s_root);
    lv_obj_set_size(s_ovr_btn, 200, 48);
    lv_obj_set_pos(s_ovr_btn, 8, y);
    lv_obj_t* ot = lv_label_create(s_ovr_btn);
    lv_label_set_text(ot, "Override");
    lv_obj_center(ot);
    lv_obj_add_event_cb(s_ovr_btn, on_override, LV_EVENT_CLICKED, nullptr);

    screen_settings_refresh();
    return s_root;
}

void screen_settings_refresh(void) {
    if (!s_root) return;
    const CoolerState& st = cooler_state();
    Commands& cmd = panel_commands();
    char buf[48];
    for (size_t i = 0; i < BOUNDS_N; i++) {
        const Bound* b = &BOUNDS[i];
        if (b->widget != Widget::Stepper || !s_val[i]) continue;
        int v = cmd.display_value(b->key, st);
        bool pending = cmd.is_pending(b->key);
        std::snprintf(buf, sizeof(buf), "%d %s%s", v, b->unit, pending ? " ..." : "");
        lv_label_set_text(s_val[i], buf);
        lv_obj_set_style_text_opa(s_val[i], pending ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
    lv_obj_set_style_bg_color(s_ovr_btn,
        st.override_on ? lv_color_hex(0xd08000) : lv_color_hex(0x303840), 0);
}
```

- [ ] **Step 4: Disable editing whenever the cooler is not in normal mode**

**This is a correctness requirement, not polish.** In `override` or
`sht30-failsafe` mode the cooler publishes EFFECTIVE values rather than stored
config — with override engaged it reports `maxrun` 15, `minofftime` 14 and
`dutypercent` equal to the potentiometer reading, not what is stored. Verified
against the captured fixtures:

    data_normal    mode=normal    maxrun=15  minofftime=5   dutypercent=80
    data_override  mode=override  maxrun=15  minofftime=14  dutypercent=37  (= pot_pct)

So while either mode is active the panel cannot see the stored settings at all.
Editing from that baseline would show pot-derived numbers as if they were config,
and `Commands` would capture an effective value as its pre-publish baseline.

In `screen_settings_refresh()`, when `strcmp(st.mode, "normal") != 0`:

- disable every stepper and chip button (`lv_obj_add_state(btn, LV_STATE_DISABLED)`)
- grey the value labels
- show a one-line banner explaining why, e.g.
  `"override active - settings read-only"` or `"sensor fault - settings read-only"`

Re-enable them when `mode` returns to `normal`. The override toggle itself stays
ENABLED — you must be able to turn override off from the panel.

The proper fix is the cooler publishing stored and effective values separately,
which needs an ESPHome change and a reflash; that is outside this plan.

- [ ] **Step 5: Drive Commands::tick from the UI loop**

In `shared/ui/ui.cpp`, in the periodic refresh, add:

```cpp
panel_commands().tick(platform_now_ms());
```

- [ ] **Step 6: Build and verify in the simulator**

Run:
```bash
cmake --build build -j
./build/cooler_sim --host mqtt.example.com --port 1883 \
    --no-tls --user <user> --pass <REDACTED - see tools/import_cooler_broker.sh>
```
Expected: the settings page shows live values; tapping `+`/`-` on **Set point** greys the value with `...`, then snaps to the cooler's confirmed value within about a second. Verify the steppers refuse to go past bounds.

- [ ] **Step 7: Run the test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS including `portability_guard`.

- [ ] **Step 8: Commit**

```bash
git add shared/ui/screen_settings.* shared/ui/ui.cpp shared/app.*
git commit -m "feat: settings screen with pending-state steppers"
```

---

# Phase D — Alarms, detail, device

### Task 11: Alarm state machine

**Files:**
- Create: `shared/model/alarm.h`, `shared/model/alarm.cpp`, `tests/test_alarm.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `CoolerState` (Task 4)
- Produces: `enum class AlarmId`, `struct AlarmCfg`, `class Alarms` with `configure(const AlarmCfg&)`, `update(const CoolerState&, int64_t)`, `active()`, `acknowledge(AlarmId, int64_t)`, `any_latched()`, `text(AlarmId)`

- [ ] **Step 1: Write the failing test**

Create `tests/test_alarm.cpp`:

```cpp
#include <doctest/doctest.h>
#include "alarm.h"
#include "cooler_state.h"

static CoolerState ok_state() {
    CoolerState s;
    s.valid = true; s.online = true;
    s.coolerset = 4; s.range = 2; s.temp = 4.0f;
    s.sht_fault = 0; s.last_rx_epoch = 1000;
    return s;
}

// Advance the clock WITH a fresh /data arrival. ControllerSilent outranks
// almost everything, so without refreshing last_rx_epoch it would fire
// incidentally in every test that advances time, masking the condition
// actually under test. The silence test below deliberately does not use this.
static void tick(Alarms& a, CoolerState& s, int64_t now) {
    s.last_rx_epoch = now;
    a.update(s, now);
}

TEST_CASE("healthy state raises nothing") {
    Alarms a; CoolerState s = ok_state();
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::None);
    CHECK_FALSE(a.any_latched());
}

TEST_CASE("offline availability fires immediately") {
    Alarms a; CoolerState s = ok_state();
    s.online = false;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::ControllerOffline);
}

TEST_CASE("silence fires only after the timeout") {
    Alarms a; CoolerState s = ok_state();
    s.last_rx_epoch = 1000;          // deliberately NOT refreshed
    a.update(s, 1299);
    CHECK(a.active(1299) == AlarmId::None);
    a.update(s, 1301);
    CHECK(a.active(1301) == AlarmId::ControllerSilent);
}

TEST_CASE("sensor fault fires immediately") {
    Alarms a; CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::SensorFault);
}

TEST_CASE("not-keeping-up needs 5C over for 60 min continuously") {
    Alarms a; CoolerState s = ok_state();
    s.temp = 12.0f;                  // coolerset 4 + range 2 + 5 = 11 threshold
    tick(a, s, 1000);
    CHECK(a.active(1000) == AlarmId::None);        // clock just started
    tick(a, s, 4599);
    CHECK(a.active(4599) == AlarmId::None);        // 3599 s elapsed
    tick(a, s, 4601);
    CHECK(a.active(4601) == AlarmId::NotKeepingUp);
}

TEST_CASE("dropping back under the threshold resets the timer") {
    Alarms a; CoolerState s = ok_state();
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
    Alarms a; CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    REQUIRE(a.active(1000) == AlarmId::SensorFault);
    a.acknowledge(AlarmId::SensorFault, 1000);     // hold-off to 2800
    CHECK(a.active(1000) == AlarmId::None);
    CHECK(a.any_latched());                        // header marker stays
    tick(a, s, 2799);
    CHECK(a.active(2799) == AlarmId::None);
    tick(a, s, 2801);
    CHECK(a.active(2801) == AlarmId::SensorFault);
}

TEST_CASE("acknowledging one condition does not suppress a different one") {
    Alarms a; CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    a.acknowledge(AlarmId::SensorFault, 1000);
    REQUIRE(a.active(1000) == AlarmId::None);

    s.online = false;                              // a DIFFERENT fault appears
    tick(a, s, 1010);
    CHECK(a.active(1010) == AlarmId::ControllerOffline);
}

TEST_CASE("a cleared condition unlatches and drops its acknowledge") {
    Alarms a; CoolerState s = ok_state();
    s.sht_fault = 1;
    tick(a, s, 1000);
    a.acknowledge(AlarmId::SensorFault, 1000);
    s.sht_fault = 0;
    tick(a, s, 1100);
    CHECK(a.active(1100) == AlarmId::None);
    CHECK_FALSE(a.any_latched());
    // Coming back is a fresh alarm, not one still inside the old hold-off.
    s.sht_fault = 1;
    tick(a, s, 1200);
    CHECK(a.active(1200) == AlarmId::SensorFault);
}

TEST_CASE("offline outranks not-keeping-up") {
    Alarms a; CoolerState s = ok_state();
    s.temp = 12.0f;
    tick(a, s, 1000);
    tick(a, s, 4601);
    REQUIRE(a.active(4601) == AlarmId::NotKeepingUp);
    s.online = false;
    tick(a, s, 4602);
    CHECK(a.active(4602) == AlarmId::ControllerOffline);
}
```

- [ ] **Step 2: Add to build and run to verify failure**

Add `tests/test_alarm.cpp` to `cooler_tests`.
Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL — `alarm.h: No such file or directory`

- [ ] **Step 3: Write alarm.h**

```cpp
#pragma once
#include <stdint.h>
struct CoolerState;

// Ordered by priority -- lower value wins when several are live at once.
enum class AlarmId { None = 0, ControllerOffline, ControllerSilent,
                     SensorFault, NotKeepingUp, COUNT };

struct AlarmCfg {
    int silent_s  = 300;    // no /data for this long -> ControllerSilent
    int over_c    = 5;      // degrees above coolerset+range
    int over_s    = 3600;   // ...sustained this long -> NotKeepingUp
    int holdoff_s = 1800;   // acknowledge suppression window
};

class Alarms {
public:
    void configure(const AlarmCfg& c) { cfg_ = c; }
    void update(const CoolerState& s, int64_t now_epoch);

    // Highest-priority condition that is live, has held long enough, and is
    // not inside its acknowledge hold-off. Needs the clock, so it takes one.
    AlarmId active(int64_t now_epoch) const;

    void acknowledge(AlarmId id, int64_t now_epoch);

    // True if any condition is currently live, acknowledged or not.
    bool any_latched() const;

    static const char* text(AlarmId id);

private:
    struct Cond {
        bool live = false;
        int64_t since = 0;       // when it first became true
        int64_t ack_until = 0;   // suppressed while now < this
    };
    AlarmCfg cfg_;
    Cond conds_[(int)AlarmId::COUNT];
};
```

- [ ] **Step 4: Write alarm.cpp**

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

    mark(AlarmId::ControllerOffline, s.valid && !s.online);
    mark(AlarmId::ControllerSilent,
         s.valid && (now_epoch - s.last_rx_epoch) > cfg_.silent_s);
    mark(AlarmId::SensorFault, s.valid && s.sht_fault != 0);
    mark(AlarmId::NotKeepingUp,
         s.valid && s.temp > (float)(s.coolerset + s.range + cfg_.over_c));
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
        case AlarmId::SensorFault:       return "SENSOR FAULT";
        case AlarmId::NotKeepingUp:      return "NOT COOLING";
        default:                         return "";
    }
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS. If the duration or hold-off tests fail, the signature change above was not applied.

- [ ] **Step 6: Commit**

```bash
git add shared/model/alarm.* tests/test_alarm.cpp CMakeLists.txt
git commit -m "feat: per-condition alarm state machine with hold-off"
```

---

### Task 12: Alarm and Detail screens

**Files:**
- Create: `shared/ui/screen_alarm.h`, `shared/ui/screen_alarm.cpp`, `shared/ui/screen_detail.h`, `shared/ui/screen_detail.cpp`
- Modify: `shared/app.cpp`, `shared/app.h`, `shared/ui/ui.cpp`

**Interfaces:**
- Consumes: `Alarms`/`AlarmId` (Task 11), `CoolerState` (Task 4)
- Produces: `screen_alarm_create(lv_obj_t*)`, `screen_alarm_refresh()`, `screen_detail_create(lv_obj_t*)`, `screen_detail_refresh()`, `panel_alarms() -> Alarms&`

- [ ] **Step 1: Expose the Alarms singleton**

In `shared/app.h`:
```cpp
class Alarms;
Alarms& panel_alarms();
```
In `shared/app.cpp`:
```cpp
#include "alarm.h"
static Alarms g_alarms;
Alarms& panel_alarms() { return g_alarms; }
```

- [ ] **Step 2: Write screen_alarm.cpp**

```cpp
#include "screen_alarm.h"
#include "alarm.h"
#include "cooler_state.h"
#include "app.h"
#include "platform.h"
#include <cstdio>

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
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x7a0d0d), 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_title = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_48, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 60);

    s_detail = lv_label_create(s_root);
    lv_obj_align(s_detail, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t* btn = lv_button_create(s_root);
    lv_obj_set_size(btn, 240, 64);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_t* t = lv_label_create(btn);
    lv_label_set_text(t, "ACKNOWLEDGE");
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

    char buf[96];
    switch (id) {
        case AlarmId::ControllerSilent:
            std::snprintf(buf, sizeof(buf), "no message for %d min",
                          (int)((platform_epoch_utc() - st.last_rx_epoch) / 60));
            break;
        case AlarmId::NotKeepingUp:
            std::snprintf(buf, sizeof(buf), "%.1f vs set %d", (double)st.temp, st.coolerset);
            break;
        default:
            std::snprintf(buf, sizeof(buf), "mode %s", st.mode);
            break;
    }
    lv_label_set_text(s_detail, buf);
}
```

- [ ] **Step 3: Write screen_detail.cpp**

```cpp
#include "screen_detail.h"
#include "cooler_state.h"
#include "app.h"
#include <cstdio>

static lv_obj_t* s_root;
static lv_obj_t* s_body;

lv_obj_t* screen_detail_create(lv_obj_t* parent) {
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, 480, 480);
    s_body = lv_label_create(s_root);
    lv_obj_set_pos(s_body, 12, 12);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_body, 456);
    screen_detail_refresh();
    return s_root;
}

void screen_detail_refresh(void) {
    if (!s_root) return;
    const CoolerState& s = cooler_state();
    char buf[640];
    std::snprintf(buf, sizeof(buf),
        "mode      %s\n"
        "lead      AC%d      active  AC%d\n"
        "window    %d/%ds  on %ds\n"
        "swap in   %ds\n"
        "AC1  %s  relay %s  blocked %d  run %um\n"
        "AC2  %s  relay %s  blocked %d  run %um\n"
        "pot       %d%%\n"
        "sht_fault %d\n"
        "uptime    %uh %um\n"
        "link      %s\n"
        "last rx   %llds ago",
        s.mode, s.lead, s.active_unit,
        s.window_pos_s, s.window_s, s.window_on_s,
        s.swap_in_s,
        s.ac1, s.ac1_relay ? "ON" : "--", s.ac1_blocked, s.ac1_runtime_s / 60,
        s.ac2, s.ac2_relay ? "ON" : "--", s.ac2_blocked, s.ac2_runtime_s / 60,
        s.pot_pct,
        s.sht_fault,
        s.uptime_s / 3600, (s.uptime_s % 3600) / 60,
        s.online ? "online" : "OFFLINE",
        (long long)(platform_epoch_utc() - s.last_rx_epoch));
    lv_label_set_text(s_body, buf);
}
```

Add matching two-line headers `screen_alarm.h` / `screen_detail.h` declaring `_create` and `_refresh` exactly as used above.

- [ ] **Step 4: Close the never-connected alarm gap (SAFETY)**

Task 11's `update()` gates every condition on `s.valid`, which only becomes true
after a successful `/data` parse. A panel that has never heard from the cooler
therefore fires nothing and is visually indistinguishable from a healthy idle
panel — the exact situation the alarms exist to catch. The retained LWT makes it
worse: a dead cooler's `offline` does reach the router and set `s.online = false`,
but `ControllerOffline` is gated `s.valid && !s.online`, so it stays silent.

Fix across three files:

1. `shared/model/cooler_state.h` — add `bool availability_seen = false;` beside
   `online`.
2. `shared/model/mqtt_router.cpp` — in the `availability` branch, set
   `s.availability_seen = true;` alongside `s.online`.
3. `shared/model/alarm.cpp` — change the ControllerOffline mark from
   `s.valid && !s.online` to `s.availability_seen && !s.online`.

Leave ControllerSilent, SensorFault and NotKeepingUp gated on `s.valid` — those
genuinely need a reading before they mean anything.

Add tests to `tests/test_alarm.cpp`:

```cpp
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
```

The second test matters: a panel still booting, or one whose broker is briefly
unreachable, must not scream before it has heard anything either way.

- [ ] **Step 5: Drive the alarm update from the message path**

In `shared/app.cpp`, after `g_cmds.on_state(g_state)`:

```cpp
g_alarms.update(g_state, platform_epoch_utc());
```

Also call `g_alarms.update(...)` from the UI tick so `ControllerSilent` fires even when no messages are arriving — that condition can never be detected from inside the message handler.

- [ ] **Step 6: Build and verify in the simulator**

Run: `cmake --build build -j && ./build/cooler_sim --host mqtt.example.com --port 1883 --no-tls --user <user> --pass <REDACTED - see tools/import_cooler_broker.sh>`

Force an alarm by killing the network to the sim (or pointing it at a dead host) and confirm the red takeover appears after 5 minutes, that ACKNOWLEDGE dismisses it, and that it returns after 30 minutes.

- [ ] **Step 7: Run the test suite and commit**

Run: `ctest --test-dir build --output-on-failure`

```bash
git add shared/ui/screen_alarm.* shared/ui/screen_detail.* shared/app.* shared/ui/ui.cpp
git commit -m "feat: alarm takeover and detail screens"
```

---

### Task 13: Swipe paging

**Files:**
- Modify: `shared/ui/nav.h`, `shared/ui/nav.cpp`, `shared/ui/ui.cpp`

**Interfaces:**
- Consumes: all four `screen_*_create` functions
- Produces: `nav_init(lv_obj_t* root)`, `nav_refresh()`

- [ ] **Step 1: Rewrite nav.cpp as a three-page tileview**

```cpp
#include "nav.h"
#include "screen_trend.h"
#include "screen_settings.h"
#include "screen_detail.h"
#include "screen_alarm.h"

static lv_obj_t* s_tv;

void nav_init(lv_obj_t* root) {
    s_tv = lv_tileview_create(root);
    lv_obj_set_size(s_tv, 480, 480);

    lv_obj_t* t0 = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t* t1 = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t* t2 = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_LEFT);

    screen_trend_create(t0);
    screen_settings_create(t1);
    screen_detail_create(t2);
    screen_alarm_create(root);      // sits above the tileview, hidden
}

void nav_refresh(void) {
    screen_trend_refresh();
    screen_settings_refresh();
    screen_detail_refresh();
    screen_alarm_refresh();         // shows/hides itself
}
```

Update `nav.h` to declare both functions.

- [ ] **Step 2: Call nav_refresh on the UI tick**

In `shared/ui/ui.cpp`, the periodic timer should call `panel_commands().tick(platform_now_ms())`, `panel_alarms().update(cooler_state(), platform_epoch_utc())`, then `nav_refresh()`.

- [ ] **Step 3: Build, run and verify paging**

Run: `cmake --build build -j && ./build/cooler_sim --host mqtt.example.com --port 1883 --no-tls --user <user> --pass <REDACTED - see tools/import_cooler_broker.sh>`
Expected: dragging left/right moves between Trend, Settings and Detail; an alarm covers all three.

- [ ] **Step 4: Run the test suite and commit**

Run: `ctest --test-dir build --output-on-failure`

```bash
git add shared/ui/nav.* shared/ui/ui.cpp
git commit -m "feat: swipe paging between trend, settings and detail"
```

---

### Task 14: Device build and flash

**Files:**
- Modify: `device/platformio.ini`, `device/src/main.cpp`, `config/device_config.cpp`

**Interfaces:**
- Consumes: everything above
- Produces: firmware running on the Waveshare Smart 86 Box

- [ ] **Step 1: Repoint the PlatformIO project name and default config**

In `device/platformio.ini`, change the env/program name from `llmmon` to `cooler_panel`. In `config/device_config.cpp`, change the default `mqtt_base` from `"llmmon"` to `"ha/esp32-cooler"`, default port to `1883`, default `mqtt_tls` to `false`.

- [ ] **Step 2: Repair device main.cpp — it currently does NOT compile**

Be clear on the starting state: `device/src/main.cpp` was carried over from
LLMMon untouched by Task 1 (deliberately, because this task owns it), so it still
`#include`s the deleted `app_model.h` and calls `app_model()` in three places.
**The device firmware has not built since the fork** — only the CMake
simulator/test build has ever been green. This is a repair, not a tidy-up, and
`pio run` will fail until it is done.

Replace the LLMMon app-layer calls with the current interfaces:
- `app_model().link()` -> `app_link_state()`
- `app_model().prune(...)` -> delete; nothing prunes in this project
- LLMMon screen calls -> `nav_init()` / `nav_refresh()`
- add `app_init_history()` after PSRAM is up and before `nav_init()`
- keep the `panel_cfg_load()` call Task 16 added before `ui_init()`

`sim/main.cpp` already uses the correct interfaces and is the reference.

- [ ] **Step 3: Apply backlight and night dimming to the hardware**

Task 16 persists and clamps `PanelConfig::backlight` (5..100) and `night_dim`,
but nothing applies either — a reviewer caught that no task owned the wiring.
`device/src/backlight.cpp` came over from LLMMon and provides the PWM control.

Apply `panel_config().backlight` at startup, after `panel_cfg_load()`. If
`night_dim` is true, reduce the backlight during night hours using the SNTP
clock already available via `platform_epoch_utc()`. Keep it simple — a fixed
dim level on a fixed hour range is fine; this does not need a schedule editor.

On the simulator there is no backlight, so `sim/` needs no change: guard the
call in the device layer rather than adding a no-op to the platform seam.

- [ ] **Step 4: Update device main.cpp wiring**

Replace LLMMon UI calls with `nav_init()` / `nav_refresh()`, call `app_init_history()` after PSRAM is up, and change the MQTT client id prefix:

```cpp
s_client_id = "cooler-panel-" + std::string(String((uint32_t)(ESP.getEfuseMac() >> 32), HEX).c_str());
```

Change the subscribe topic in `mqtt_pubsub.cpp` from `mqtt_base + "/#"` to two explicit subscriptions:

```cpp
s_mqtt.subscribe((s_cfg.mqtt_base + "/data").c_str(), 1);
s_mqtt.subscribe((s_cfg.mqtt_base + "/availability").c_str(), 1);
```

- [ ] **Step 5: Reduce the history allocation for the device if PSRAM is tight**

`app_init_history()` already falls back from 20160 to 2880 samples. Confirm from the serial log which one succeeded.

- [ ] **Step 6: Build the device firmware**

Run:
```bash
cd ~/projects/CoolerPanel/device
pio run
```
Expected: compiles. Fix any Arduino/ESP-IDF-specific breakage in the new model files (they should be clean — `portability_guard` already proved they touch nothing platform-specific).

- [ ] **Step 7: Flash and provision**

Run: `pio run -t upload && pio device monitor`

On first boot the captive portal appears. Join the `cooler-panel` AP and set WiFi `<your-ssid>`, broker `mqtt.example.com:1883`, the broker user, password `<REDACTED - see tools/import_cooler_broker.sh>`, base topic `ha/esp32-cooler`, TLS off.

- [ ] **Step 8: Verify on hardware**

Confirm: live temperature matches `mosquitto_sub` output; the trend fills in over 30 minutes; a setpoint change from the panel appears on `/cmd` and echoes back in `/data`; unplugging the cooler raises the offline alarm within seconds.

- [ ] **Step 9: Commit**

```bash
cd ~/projects/CoolerPanel
git add device/ config/
git commit -m "feat: device build, provisioning defaults and flash"
```

---

### Task 15: Preset chip rows

**Files:**
- Modify: `shared/ui/screen_settings.cpp`

**Interfaces:**
- Consumes: `BOUNDS`/`Widget::Preset` (Task 3), `Commands` (Task 9)
- Produces: nothing new — completes the Settings screen

- [ ] **Step 1: Add the preset tables**

At the top of `screen_settings.cpp`:

```cpp
// Stepping to an hour at 10 s a tap is absurd, so the two wide-range values
// get fixed chips instead. Labels and values are parallel arrays.
struct PresetSet { const char* key; const int* vals; const char* const* labels; int n; };

static const int  kScreenVals[]   = {0, 60, 300, 900, 3600};
static const char* const kScreenLbls[] = {"off", "1m", "5m", "15m", "1h"};
static const int  kSampleVals[]   = {10, 60, 300, 900, 3600};
static const char* const kSampleLbls[] = {"10s", "1m", "5m", "15m", "1h"};

static const PresetSet kPresets[] = {
    {"screentimeout",  kScreenVals, kScreenLbls, 5},
    {"sampleinterval", kSampleVals, kSampleLbls, 5},
};
static const int kPresetsN = 2;

static const PresetSet* preset_for(const char* key) {
    for (int i = 0; i < kPresetsN; i++)
        if (!std::strcmp(kPresets[i].key, key)) return &kPresets[i];
    return nullptr;
}
```

- [ ] **Step 2: Add the chip click handler and render loop**

```cpp
struct ChipCtx { const char* key; int value; };
static ChipCtx s_chips[16];
static lv_obj_t* s_chip_obj[16];
static int s_chip_n = 0;

static void on_chip(lv_event_t* e) {
    ChipCtx* c = (ChipCtx*)lv_event_get_user_data(e);
    panel_commands().request(c->key, c->value, platform_now_ms());
    screen_settings_refresh();
}
```

In `screen_settings_create`, after the stepper loop and before the override button, add:

```cpp
for (size_t i = 0; i < BOUNDS_N; i++) {
    const Bound* b = &BOUNDS[i];
    if (b->widget != Widget::Preset) continue;
    const PresetSet* p = preset_for(b->key);
    if (!p) continue;

    lv_obj_t* lbl = lv_label_create(s_root);
    lv_label_set_text(lbl, b->label);
    lv_obj_set_pos(lbl, 8, y + 14);

    for (int k = 0; k < p->n && s_chip_n < 16; k++) {
        lv_obj_t* chip = lv_button_create(s_root);
        lv_obj_set_size(chip, 76, 40);
        lv_obj_set_pos(chip, 150 + k * 66, y + 4);
        lv_obj_t* t = lv_label_create(chip);
        lv_label_set_text(t, p->labels[k]);
        lv_obj_center(t);
        s_chips[s_chip_n] = ChipCtx{b->key, p->vals[k]};
        lv_obj_add_event_cb(chip, on_chip, LV_EVENT_CLICKED, &s_chips[s_chip_n]);
        s_chip_obj[s_chip_n] = chip;
        s_chip_n++;
    }
    y += ROW_H;
}
```

- [ ] **Step 3: Highlight the selected chip in refresh**

At the end of `screen_settings_refresh()`:

```cpp
for (int i = 0; i < s_chip_n; i++) {
    int cur = cmd.display_value(s_chips[i].key, st);
    bool sel = (cur == s_chips[i].value);
    lv_obj_set_style_bg_color(s_chip_obj[i],
        sel ? lv_color_hex(0x2b6cb0) : lv_color_hex(0x303840), 0);
}
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build -j && ./build/cooler_sim --host mqtt.example.com --port 1883 --no-tls --user <user> --pass <REDACTED - see tools/import_cooler_broker.sh>`
Expected: two chip rows appear; the chip matching the live value is highlighted; tapping `5m` on LCD timeout publishes `{"screentimeout":300}` and the highlight follows the cooler's confirmation.

- [ ] **Step 5: Run tests and commit**

Run: `ctest --test-dir build --output-on-failure`

```bash
git add shared/ui/screen_settings.cpp
git commit -m "feat: preset chip rows for screentimeout and sampleinterval"
```

---

### Task 16: Panel-local config

**Files:**
- Create: `shared/model/panel_config.h`, `shared/model/panel_config.cpp`, `tests/test_panel_config.cpp`
- Modify: `CMakeLists.txt`, `shared/app.cpp`, `device/src/config_nvs.cpp`, `sim/config.cpp`

**Interfaces:**
- Consumes: `AlarmCfg` (Task 11)
- Produces: `struct PanelConfig`, `panel_config_to_json(const PanelConfig&) -> std::string`, `panel_config_from_json(const char*, PanelConfig&) -> bool`, `panel_config() -> PanelConfig&`

- [ ] **Step 1: Write the failing test**

Create `tests/test_panel_config.cpp`:

```cpp
#include <doctest/doctest.h>
#include "panel_config.h"

TEST_CASE("defaults match the design spec") {
    PanelConfig c;
    CHECK(c.alarm.silent_s == 300);
    CHECK(c.alarm.over_c == 5);
    CHECK(c.alarm.over_s == 3600);
    CHECK(c.alarm.holdoff_s == 1800);
    CHECK(c.backlight == 80);
    CHECK(c.night_dim == true);
    CHECK(c.default_zoom_s == 3600);
}

TEST_CASE("json round trip preserves every field") {
    PanelConfig c;
    c.alarm.silent_s = 120; c.alarm.over_c = 3;
    c.alarm.over_s = 900;   c.alarm.holdoff_s = 600;
    c.backlight = 40; c.night_dim = false; c.default_zoom_s = 86400;

    PanelConfig r;
    REQUIRE(panel_config_from_json(panel_config_to_json(c).c_str(), r));
    CHECK(r.alarm.silent_s == 120);
    CHECK(r.alarm.over_c == 3);
    CHECK(r.alarm.over_s == 900);
    CHECK(r.alarm.holdoff_s == 600);
    CHECK(r.backlight == 40);
    CHECK(r.night_dim == false);
    CHECK(r.default_zoom_s == 86400);
}

TEST_CASE("absent keys fall back to defaults") {
    PanelConfig r;
    REQUIRE(panel_config_from_json("{\"backlight\":25}", r));
    CHECK(r.backlight == 25);
    CHECK(r.alarm.silent_s == 300);      // default preserved
    CHECK(r.default_zoom_s == 3600);
}

TEST_CASE("malformed json is rejected") {
    PanelConfig r;
    CHECK_FALSE(panel_config_from_json("{nope", r));
}

TEST_CASE("out-of-range values are clamped on load") {
    PanelConfig r;
    REQUIRE(panel_config_from_json(
        "{\"backlight\":999,\"alarm\":{\"holdoff_s\":-5}}", r));
    CHECK(r.backlight == 100);
    CHECK(r.alarm.holdoff_s == 0);
}
```

- [ ] **Step 2: Add to build and run to verify failure**

Add `tests/test_panel_config.cpp` to `cooler_tests`.
Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL — `panel_config.h: No such file or directory`

- [ ] **Step 3: Write panel_config.h**

```cpp
#pragma once
#include <string>
#include "alarm.h"

// Settings that belong to the panel, not the cooler. Never published to
// ha/esp32-cooler/cmd -- the controller neither knows nor cares about these.
struct PanelConfig {
    AlarmCfg alarm;              // thresholds + hold-off (spec section 7)
    int  backlight = 80;         // percent, 5..100
    bool night_dim = true;
    int  default_zoom_s = 3600;  // 3600 | 86400 | 604800
};

std::string panel_config_to_json(const PanelConfig& c);
bool panel_config_from_json(const char* json, PanelConfig& out);
```

- [ ] **Step 4: Write panel_config.cpp**

```cpp
#include "panel_config.h"
#include <ArduinoJson.h>

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

std::string panel_config_to_json(const PanelConfig& c) {
    JsonDocument d;
    auto a = d["alarm"].to<JsonObject>();
    a["silent_s"]  = c.alarm.silent_s;
    a["over_c"]    = c.alarm.over_c;
    a["over_s"]    = c.alarm.over_s;
    a["holdoff_s"] = c.alarm.holdoff_s;
    d["backlight"]      = c.backlight;
    d["night_dim"]      = c.night_dim;
    d["default_zoom_s"] = c.default_zoom_s;
    std::string out;
    serializeJson(d, out);
    return out;
}

bool panel_config_from_json(const char* json, PanelConfig& out) {
    JsonDocument d;
    if (deserializeJson(d, json) != DeserializationError::Ok) return false;
    PanelConfig c;   // start from defaults so absent keys keep them
    if (d["alarm"].is<JsonObject>()) {
        auto a = d["alarm"];
        c.alarm.silent_s  = a["silent_s"]  | c.alarm.silent_s;
        c.alarm.over_c    = a["over_c"]    | c.alarm.over_c;
        c.alarm.over_s    = a["over_s"]    | c.alarm.over_s;
        c.alarm.holdoff_s = a["holdoff_s"] | c.alarm.holdoff_s;
    }
    c.backlight      = d["backlight"]      | c.backlight;
    c.night_dim      = d["night_dim"]      | c.night_dim;
    c.default_zoom_s = d["default_zoom_s"] | c.default_zoom_s;

    c.alarm.silent_s  = clampi(c.alarm.silent_s,  30, 3600);
    c.alarm.over_c    = clampi(c.alarm.over_c,     1,   20);
    c.alarm.over_s    = clampi(c.alarm.over_s,    60, 86400);
    c.alarm.holdoff_s = clampi(c.alarm.holdoff_s,  0, 86400);
    c.backlight       = clampi(c.backlight,        5,  100);
    if (c.default_zoom_s != 3600 && c.default_zoom_s != 86400 &&
        c.default_zoom_s != 604800) c.default_zoom_s = 3600;

    out = c;
    return true;
}
```

- [ ] **Step 5: Wire the singleton and apply it at startup**

In `shared/app.h`:
```cpp
struct PanelConfig;
PanelConfig& panel_config();
```
In `shared/app.cpp`:
```cpp
#include "panel_config.h"
static PanelConfig g_pcfg;
PanelConfig& panel_config() { return g_pcfg; }
```
In `ui_init()`, after `nav_init()`, apply it:
```cpp
panel_alarms().configure(panel_config().alarm);
trend_set_zoom(panel_config().default_zoom_s);
```

- [ ] **Step 6: Persist it per platform**

Device: in `device/src/config_nvs.cpp`, add `panel_cfg_load()` / `panel_cfg_save()` storing the JSON string under NVS key `panelcfg`, following the existing `config_nvs` read/write pattern in that file. Call `panel_cfg_load()` before `ui_init()`.

Simulator: in `sim/config.cpp`, read/write `~/.cooler_panel.json` with the same JSON, so simulator runs keep their settings too.

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add shared/model/panel_config.* tests/test_panel_config.cpp \
        shared/app.* device/src/config_nvs.cpp sim/config.cpp CMakeLists.txt
git commit -m "feat: panel-local config for alarm thresholds, backlight and zoom"
```

---

## Self-Review Notes

**Spec coverage — every section maps to at least one task:**

| Spec section | Tasks |
|---|---|
| §1 Hardware | 14 |
| §2 Architecture | 1, 8 |
| §3 MQTT contract | 4, 8, 14 |
| §4 Data model | 3, 4, 5 |
| §5 Command path | 9 |
| §6 Screens | 7, 10, 12, 13, 15 |
| §7 Alarms | 11, 12 |
| §8 Panel-local config | 16 |
| Boot splash + setup portal (added post-approval) | 17 |
| §9 Error handling | 4, 9, 11 |
| §10 Testing | 2, plus a test step in every task |
| §11 Build order | phase structure |

**Three issues found and fixed during review:**

1. **`Alarms::active()` could not see the clock.** It cannot evaluate the `over_s` duration or hold-off expiry without it, and the sketch referenced the private `Cond` type from a free function, which would not compile. Signature is now `active(int64_t now_epoch)`, fully implemented.

2. **The alarm tests would have failed for the wrong reason.** `ControllerSilent` outranks `SensorFault` and `NotKeepingUp`, so any test advancing the clock without refreshing `last_rx_epoch` trips it incidentally and masks the condition under test. Added a `tick()` helper that advances time *with* a fresh `/data`; the silence test alone deliberately does not use it.

3. **`PubRecord` was defined in both the stub header and its `.cpp`** — an ODR violation. Now defined once in the header, with the required `target_include_directories(cooler_tests PRIVATE tests)` added.

**Two gaps closed by adding tasks:** preset chips (Task 15) and panel-local config (Task 16), neither of which had an owner in the first draft.

**Type consistency verified across tasks:** `bounds_clamp(key, v, cur_minofftime)`, `route_message(CoolerState&, topic, payload, len, now_epoch)`, `chart_downsample(h, from, to, cols, ncols)`, `Commands::display_value(key, state)`, `Alarms::active(now)`, and the `panel_history()` / `panel_commands()` / `panel_alarms()` / `panel_config()` / `cooler_state()` accessor family are used identically everywhere they appear.

---

### Task 17: Plain MQTT, boot splash, and setup portal

Added after the plan was approved, at the user's request. Three things: strip
TLS entirely (plain MQTT only), restore LLMMon's boot splash, and offer a
WiFi/broker setup page whose broker fields default to the cooler's own.

**Runs BEFORE Task 14** — the device flash depends on these defaults existing.

**Files:**
- Remove TLS from: `config/device_config.{h,cpp}`, `config/tests/test_device_config.cpp`, `device/src/{config_nvs,mqtt_pubsub,portal}.cpp`, `device/tools/gen_assets.sh`, `sim/{config.h,config.cpp,mqtt_mosq.cpp}`
- Create: `assets/brand/llmmon-logo-{dark,light}.png` (copied), `tools/import_cooler_broker.sh`, `config/cooler_defaults.h` (generated, committed)
- Modify: `shared/ui/ui.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `boot_build` (screen_boot.cpp), `screen_setup.cpp`, `DeviceConfig`
- Produces: `COOLER_DEFAULT_*` macros; a boot -> setup -> main flow in `ui_init()`

- [ ] **Step 1: Strip TLS from the portable config**

In `config/device_config.h` remove the `mqtt_tls` and `ca_pem` members. In
`config/device_config.cpp` remove the `ca_pem` presence check that currently
gates on `mqtt_tls`, and drop both keys from `config_to_json` and
`config_from_json`.

`config_from_json` must still ACCEPT and ignore a stored config that contains
the old `tls`/`ca_pem` keys — a panel flashed with the previous build will have
one in NVS, and it must not fail to load. Ignoring unknown keys is already the
behaviour; just confirm it and do not add a rejection.

- [ ] **Step 2: Update the config tests**

`config/tests/test_device_config.cpp` sets `mqtt_tls` and `ca_pem` in its
round-trip cases. Remove those assignments and assertions. Add one case proving
forward-compatibility with the old shape:

```cpp
TEST_CASE("a config saved by the old TLS build still loads") {
    const char* legacy = R"({"wifi":{"ssid":"s","password":"p"},
      "mqtt":{"host":"h","port":8883,"username":"u","password":"pw",
              "tls":true,"base_topic":"t","ca_pem":"-----BEGIN CERTIFICATE-----"}})";
    DeviceConfig r;
    REQUIRE(config_from_json(legacy, r));
    CHECK(r.mqtt_host == "h");
    CHECK(r.mqtt_base == "t");
}
```

- [ ] **Step 3: Strip TLS from the device and simulator clients**

`device/src/mqtt_pubsub.cpp`: drop the `WiFiClientSecure` member, the
`setCACert` call and the tls branch; always use the plain `WiFiClient`. Remove
the now-unused include.

`sim/mqtt_mosq.cpp`: drop the `mosquitto_tls_set` block.

`sim/config.{h,cpp}`: remove the `tls` and `ca` fields and the `--no-tls` flag.
Note Task 7's fix added `--user`/`--pass` parsing here — preserve it.

`device/src/config_nvs.cpp`: remove any ca_pem persistence.

`device/tools/gen_assets.sh`: remove the `ca_seed.h` generation only; leave
`config_seed.h` alone. Delete the stale `device/src/assets_gen/ca_seed.h` from
disk and drop its line from `.gitignore`.

- [ ] **Step 4: Strip TLS from the captive portal form**

`device/src/portal.cpp` renders a TLS checkbox ("TLS (port 8883)") and a CA
certificate textarea, and reads `tls`/`ca` args back. Remove all four. Pasting a
PEM into a textarea on a 4-inch touchscreen was never a good experience; this is
the main user-facing win of going plain-MQTT-only.

- [ ] **Step 5: Carry the brand images over**

They live at LLMMon's repo root, not under `firmware/`, so Task 1's rsync missed
them and the simulator currently logs `logo_load: failed to open`.

```bash
mkdir -p assets/brand
cp ~/projects/LLMMon/assets/brand/llmmon-logo-dark.png assets/brand/
cp ~/projects/LLMMon/assets/brand/llmmon-logo-light.png assets/brand/
```

Keep the filenames — `sim/logo_load.cpp` and the device's generated
`assets_gen/logo_wordmark.c` both already reference them.

- [ ] **Step 6: Write the broker-import script**

Generate the defaults from the cooler's own ESPHome config rather than
hand-copying credentials. Create `tools/import_cooler_broker.sh`:

```bash
#!/usr/bin/env bash
# Generate config/cooler_defaults.h from the ESPHome cooler project, so the
# setup portal offers the right broker without anyone retyping credentials.
# Re-run if the cooler's broker settings change.
set -euo pipefail
YAML="${1:-$HOME/Arduino/ESPHome/Cooler/configc32-dual-v3.yaml}"
OUT="$(cd "$(dirname "$0")/.." && pwd)/config/cooler_defaults.h"
[ -f "$YAML" ] || { echo "cooler config not found: $YAML" >&2; exit 1; }

get() { grep -oP "^\s{2}$1:\s*\K.*" "$YAML" | head -1 | tr -d '"' | tr -d "'"; }
HOST=$(get broker); PORT=$(get port); USER=$(get username); PASS=$(get password)
PREFIX=$(grep -oP '^\s{2}topic_prefix:\s*\K.*' "$YAML" | head -1 | tr -d '"' \
         | sed 's/\${device_name}/esp32-cooler/')
: "${HOST:?could not parse broker}" "${PORT:?}" "${USER:?}" "${PASS:?}" "${PREFIX:?}"

cat > "$OUT" <<EOF
// GENERATED by tools/import_cooler_broker.sh -- do not edit by hand.
// Source: $YAML
#pragma once
#define COOLER_DEFAULT_MQTT_HOST "$HOST"
#define COOLER_DEFAULT_MQTT_PORT $PORT
#define COOLER_DEFAULT_MQTT_USER "$USER"
#define COOLER_DEFAULT_MQTT_PASS "$PASS"
#define COOLER_DEFAULT_MQTT_BASE "$PREFIX"
EOF
echo "wrote $OUT:"; grep -v PASS "$OUT" | tail -4
```

```bash
chmod +x tools/import_cooler_broker.sh && ./tools/import_cooler_broker.sh
```

Expected: HOST `mqtt.example.com`, PORT 1883, BASE `ha/esp32-cooler`.
There is deliberately no TLS macro — plain MQTT is the only mode now.

- [ ] **Step 7: Apply the defaults, test-first**

Add to `config/tests/test_device_config.cpp`:

```cpp
TEST_CASE("a fresh DeviceConfig points at the cooler's broker") {
    DeviceConfig c;
    CHECK(c.mqtt_host == "mqtt.example.com");
    CHECK(c.mqtt_port == 1883);
    CHECK(c.mqtt_user == "<user>");
    CHECK(c.mqtt_base == "ha/esp32-cooler");
    CHECK_FALSE(c.configured);   // still needs WiFi before it is usable
}
```

Run it, confirm it FAILS. Then in `config/device_config.h` include
`cooler_defaults.h` and give the members those defaults:

```cpp
std::string mqtt_host = COOLER_DEFAULT_MQTT_HOST;
uint16_t    mqtt_port = COOLER_DEFAULT_MQTT_PORT;
std::string mqtt_user = COOLER_DEFAULT_MQTT_USER;
std::string mqtt_pass = COOLER_DEFAULT_MQTT_PASS;
std::string mqtt_base = COOLER_DEFAULT_MQTT_BASE;
```

Confirm it passes. A never-configured panel now shows a setup form already
filled in with the right broker; the user supplies only WiFi.

- [ ] **Step 8: Wire the boot -> setup -> main flow**

`shared/ui/ui.cpp` was stripped in Task 1 and currently reaches neither
`screen_boot.cpp` nor `screen_setup.cpp`, so both are dead code. Read all three
files plus `config/device_config.cpp`'s boot-mode logic, then restore:

- on boot, show the splash (`boot_build`) with the wordmark and logo images
- tapping the splash's gear, OR a config whose `configured` flag is false,
  enters the setup screen
- otherwise, after the splash, hand off to `nav_init()` (Task 13's tileview)

Use LLMMon's original wiring as the reference — read
`~/projects/LLMMon/firmware/shared/ui/ui.cpp` — rather than inventing a flow.

- [ ] **Step 9: Verify in the simulator**

```bash
cmake --build build -j
./build/cooler_sim --screenshot /tmp/boot.png --screen boot
./build/cooler_sim --screenshot /tmp/setup.png --screen setup
./build/cooler_sim --host mqtt.example.com --port 1883 \
    --user <user> --pass <REDACTED - see tools/import_cooler_broker.sh>
```

Expected: splash renders the logo with no `logo_load: failed to open`; the setup
screen shows broker fields pre-filled and NO TLS checkbox or CA textarea; the
live run still connects and renders. Note `--no-tls` is gone — plain is the only
mode, so the flag has no meaning.

- [ ] **Step 10: Run the suite and commit**

```bash
ctest --test-dir build --output-on-failure
git add -A
git commit -m "feat: plain MQTT only, boot splash, setup portal with cooler defaults"
```

**Security note:** `config/cooler_defaults.h` contains the broker password in
plaintext and IS committed, matching how the cooler's own YAML already stores
it. With TLS gone, that password also crosses the network in the clear on every
connect — which was already true of the cooler and Home Assistant. If that ever
becomes unacceptable, the transport decision needs revisiting, not just this file.
