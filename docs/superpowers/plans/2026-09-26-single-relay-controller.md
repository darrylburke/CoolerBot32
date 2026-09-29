# Single-Relay Cooler Controller (v4) — Controller Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the v4 ESP32-S3 ESPHome controller: one relay fakes the Frigidaire's CN3 room thermistor, the reused thermistor guards the coil against icing, and state is published to MQTT as `/data` v2.

**Architecture:** All decisions live in `controller/cooler_logic.h`, a dependency-free C++17 header with one entry point, `cooler::step()`. It is unit-tested and simulated against a thermal plant model on the host with doctest/CMake. `controller/configs3-single.yaml` does wiring and I/O only. It calls `step()` once a second through a small glue header (`cooler_esphome.h`) that holds the state and persists settings through ESPHome preferences.

**Tech Stack:** ESPHome 2025.2 (Arduino framework, `esp32-s3-devkitc-1`), C++17, CMake ≥ 3.20, doctest 2.4.11 (FetchContent), g++ 13.

**Spec:** `docs/superpowers/specs/2026-09-26-single-relay-controller-design.md`. This plan covers spec phases A, B and D (§7). The CoolerPanel changes (phase C, spec §4) are a separate plan.

**Provenance:** every code block in this plan was compiled and run before the plan was written. The host tests (37 cases) pass with `-Wall -Wextra -Werror`, and the YAML passes `esphome config` and `esphome compile` for the ESP32-S3. Copy blocks verbatim.

## Global Constraints

- MQTT prefix `ha/esp32-cooler`. `/data` retained with `"v": 2`. `/availability` `online` / `offline` (LWT). `/cmd` JSON.
- Override thresholds are compile-time only: `OVR_ON_C = 5.0`, `OVR_OFF_C = 3.0`. They are never settable over MQTT.
- Override is in force when the latching switch (GPIO6 to GND) is closed OR MQTT has been disconnected for 60 s. It clears 60 s after reconnect.
- Pins: SHT30 SDA 8 / SCL 9. Fin ADC GPIO4 (ADC1). Relay IN1 GPIO5 (inverted: LOW = closed). Override GPIO6. LED GPIO7.
- Fin divider: 3.3 V → 33 kΩ → GPIO4 → thermistor → GND. Default Beta 3950, R0 10 kΩ at 25 °C.
- The relay is open at boot. `minofftime` counts from boot. Nothing energises the relay from a boot event.
- Settings bounds and defaults are exactly spec §2.11. Every write is clamped.
- `cooler_logic.h` must not include any ESPHome or Arduino header (host tests compile it).
- No credentials in tracked files. The YAML uses `!secret`, and `controller/secrets.yaml` is git-ignored.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

These are inputs the spec implies but doesn't spell out. Each is pinned by a test in the task that owns it:

1. **`millis()` rollover at 49.7 days.** A controller that has been up for weeks must keep honouring `minofftime` and the thermostat across the wrap. Test: "millis() wrap…" (Task 2).
2. **A setting changed partway through a run** (e.g. `maxrun` shortened below the time already run). The new value applies on the next tick, not at the next cycle. Test: "settings changed mid-run…" (Task 2).
3. **Fin ADC noise** (±0.15 °C jitter on a still coil) must not make compressor inference report "running", which would also mask `no_response`. Test: "fin ADC noise…" (Task 2).
4. **Calibration fed nonsense** (fin resistance flat while the box moves): the fit is rejected and the previous Beta kept. Test: "calibration fit rejected…" (Task 2).
5. **Malformed `/cmd` values** (floats like `4.5`, strings, unknown keys) are ignored, not coerced. This lives in the YAML, so it has no host test. It is checked by hand with `mosquitto_pub` in Task 4, Step 7.

## File Structure

| File | Responsibility |
|---|---|
| `controller/cooler_logic.h` | Settings + clamps, fin conversion, Beta fit, `step()` (modes, protections, inference, calibration). Pure C++. |
| `controller/cooler_esphome.h` | ESPHome glue: owns the Settings / FinCal / LogicState / Outputs instances, and loads and saves them through preferences. |
| `controller/configs3-single.yaml` | Board, sensors, relay, switch, LED, MQTT, `/cmd` parsing, `/data` publishing, history. No decisions. |
| `controller/secrets.yaml.example` | Template for the git-ignored `secrets.yaml`. |
| `controller/tests/CMakeLists.txt` | Host test build (globs `test_*.cpp`). |
| `controller/tests/harness.h` | `volts_for_c()` plus `Rig`, a one-second-per-tick controller driver. |
| `controller/tests/plant.h` | Thermal model of box + Frigidaire for the scenario sims. |
| `controller/tests/test_settings.cpp` | Clamps, conversion, Beta fit. |
| `controller/tests/test_step.cpp` | `step()` unit tests. |
| `controller/tests/test_scenarios.cpp` | Multi-hour closed-loop sims against `plant.h`. |
| `RULES-v4.md` | Operator-facing rules and the install checklist. |

---

### Task 0: Put the Cooler directory under git

`~/Cooler` was not yet a repo (only `CoolerPanel/` inside it was). This task creates one without swallowing the nested repo or the v3 files that contain plaintext credentials.

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Confirm the directory is not already a repo**

Run: `git -C ~/Cooler rev-parse --show-toplevel`
Expected: `fatal: not a git repository`. If it prints a path instead, skip Step 3's `git init` and continue.

- [ ] **Step 2: Replace `.gitignore`**

```gitignore
# ESPHome build state and credentials
/.esphome/
**/.esphome/
secrets.yaml

# Nested repo with its own history
/CoolerPanel/

# v3 configs carry plaintext WiFi/MQTT credentials -- never track them
/configc32-*.yaml

# Host test build output
/controller/build/
/controller/compile.log

# Scratch / images
*.png
*.ttf
/dep/
```

- [ ] **Step 3: Init and make the first commit**

```bash
cd ~/Cooler
git init
git add .gitignore RULES.md fridigaire.md docs/superpowers/specs/2026-09-26-single-relay-controller-design.md
git status --short   # expect exactly these 4 files staged; no configc32-*.yaml, no CoolerPanel/
git commit -m "chore: track cooler docs and v4 spec

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 1: Settings, fin conversion and Beta fit

**Files:**
- Create: `controller/cooler_logic.h`
- Create: `controller/tests/CMakeLists.txt`, `controller/tests/test_main.cpp`, `controller/tests/harness.h`, `controller/tests/test_settings.cpp`

**Interfaces:**
- Produces (namespace `cooler`):
  - `struct Settings { int coolerset, range, fin_cutoff, fin_recover, settle, minofftime, minruntime, maxrun, dutypercent, sampleinterval; }` with spec defaults
  - `void clamp_settings(Settings&)`, `int clampi(int, int, int)`
  - `struct FinCal { float beta; float r0; bool calibrated; float last_err; }`, `void fincal_reset(FinCal&)`
  - `float fin_ohms_from_volts(float v)` (NAN outside 0 < v < 3.3), `float fin_c_from_ohms(float ohms, const FinCal&)`
  - `struct CalPoint { float ohms; float temp_c; }`, `struct CalFit { bool ok; float beta; float r0; float err_c; }`, `CalFit fit_beta(const CalPoint*, int n)`
  - all constants (`OVR_ON_C`, `FIN_R_FIXED`, `SHT_FAULT_MS`, …)
- Produces (namespace `th`, tests only): `float volts_for_c(float temp_c, float beta = 3950, float r0 = 10000)`

- [ ] **Step 1: Create the test build and the failing tests**

`controller/tests/CMakeLists.txt`:
```cmake
# Host tests for cooler_logic.h. From the controller/ directory:
#   cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(cooler_controller_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)

enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(controller_tests ${TEST_SRC})
target_include_directories(controller_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(controller_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(controller_tests PRIVATE doctest::doctest)
add_test(NAME controller COMMAND controller_tests)
```

`controller/tests/test_main.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
```

`controller/tests/harness.h` (Task 2 replaces this with a larger version):
```cpp
// Test helpers shared by the unit tests and the scenario sims.
#pragma once
#include "cooler_logic.h"

namespace th {

// ADC volts the divider produces for a thermistor at temp_c (Beta 3950, 10k).
inline float volts_for_c(float temp_c, float beta = 3950.0f, float r0 = 10000.0f) {
    float r = r0 * std::exp(beta * (1.0f / (temp_c + 273.15f) - 1.0f / cooler::T0_K));
    return cooler::FIN_VREF * r / (r + cooler::FIN_R_FIXED);
}

}  // namespace th
```

`controller/tests/test_settings.cpp`:
```cpp
#include "doctest/doctest.h"
#include "harness.h"

using namespace cooler;

TEST_CASE("clamp_settings: every bound, lo-1 / lo / hi / hi+1") {
    struct B { int Settings::*f; int lo, hi; };
    const B bounds[] = {
        {&Settings::coolerset, 2, 40}, {&Settings::range, 0, 5},
        {&Settings::settle, 2, 30}, {&Settings::minofftime, 0, 30},
        {&Settings::minruntime, 0, 600}, {&Settings::maxrun, 1, 60},
        {&Settings::dutypercent, 1, 100}, {&Settings::sampleinterval, 10, 3600},
        {&Settings::fin_cutoff, -5, 5},
    };
    for (auto b : bounds) {
        Settings s; s.*b.f = b.lo - 1; clamp_settings(s); CHECK(s.*b.f == b.lo);
        s = Settings{}; s.*b.f = b.lo; clamp_settings(s); CHECK(s.*b.f == b.lo);
        s = Settings{}; s.*b.f = b.hi; clamp_settings(s); CHECK(s.*b.f == b.hi);
        s = Settings{}; s.*b.f = b.hi + 1; clamp_settings(s); CHECK(s.*b.f == b.hi);
    }
}

TEST_CASE("clamp_settings: fin_recover floor tracks fin_cutoff") {
    Settings s; s.fin_cutoff = 4; s.fin_recover = 3; clamp_settings(s);
    CHECK(s.fin_recover == 5);
    s = Settings{}; s.fin_recover = 11; clamp_settings(s); CHECK(s.fin_recover == 10);
    s = Settings{}; s.fin_cutoff = 0; s.fin_recover = 0; clamp_settings(s); CHECK(s.fin_recover == 1);
}

TEST_CASE("fin conversion round-trips and fit_beta recovers a known curve") {
    FinCal c;
    CHECK(fin_c_from_ohms(10000.0f, c) == doctest::Approx(25.0f).epsilon(0.001));
    float v = th::volts_for_c(0.0f);
    CHECK(fin_c_from_ohms(fin_ohms_from_volts(v), c) == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(std::isnan(fin_ohms_from_volts(3.3f)));

    CalPoint p[6];
    float temps[6] = {4, 8, 12, 16, 20, 24};
    for (int i = 0; i < 6; i++) {
        float r = 12000.0f * std::exp(3600.0f * (1.0f / (temps[i] + 273.15f) - 1.0f / T0_K));
        p[i] = CalPoint{r, temps[i]};
    }
    CalFit f = fit_beta(p, 6);
    CHECK(f.ok);
    CHECK(f.beta == doctest::Approx(3600.0f).epsilon(20.0 / 3600));
    CHECK(f.r0 == doctest::Approx(12000.0f).epsilon(0.02));
    CHECK(f.err_c < 0.05f);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd ~/Cooler/controller && cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: compile error, `cooler_logic.h: No such file or directory`.

- [ ] **Step 3: Implement `controller/cooler_logic.h`**

```cpp
// cooler_logic.h -- every control decision for the v4 single-relay cooler.
//
// Pure C++17: no ESPHome, Arduino or libc beyond <cmath>/<cstdint>. The
// ESPHome YAML fills Inputs from the hardware once a second, calls step(),
// and drives the relay / LED from the returned Outputs. Host tests compile
// this same header (controller/tests).
//
// Spec: docs/superpowers/specs/2026-09-26-single-relay-controller-design.md
#pragma once
#include <cmath>
#include <cstdint>

#ifndef COOLER_OVR_ON_C
#define COOLER_OVR_ON_C 5.0f
#endif
#ifndef COOLER_OVR_OFF_C
#define COOLER_OVR_OFF_C 3.0f
#endif

namespace cooler {

// ---- fixed constants (spec §1.4, §2.1, §2.2, §2.8, §2.9) -------------------
constexpr float OVR_ON_C = COOLER_OVR_ON_C;   // override thermostat: on at >=
constexpr float OVR_OFF_C = COOLER_OVR_OFF_C; // override thermostat: off at <=

constexpr float FIN_R_FIXED = 33000.0f;  // divider resistor, 3.3 V -> R -> ADC
constexpr float FIN_VREF = 3.3f;
constexpr float FIN_V_MIN = 0.05f, FIN_V_MAX = 3.2f;
constexpr float FIN_OHMS_MIN = 200.0f, FIN_OHMS_MAX = 1.0e6f;
constexpr float T0_K = 298.15f;          // Beta reference temperature (25 C)

constexpr uint32_t SHT_FAULT_MS = 300000;   // no valid SHT30 reading for 300 s
constexpr uint32_t FIN_DEBOUNCE_MS = 10000; // fin fault set / clear debounce
constexpr uint32_t LINK_DEBOUNCE_MS = 60000;// link_lost set / clear debounce

constexpr uint32_t FIN_SAMPLE_MS = 5000;    // fin ring cadence
constexpr int SLOPE_N = 12;                 // 12 x 5 s = 60 s slope window
constexpr int SLOPE_MIN_N = 6;              // need 30 s of samples for a slope
constexpr float COMP_ON_SLOPE = -0.5f;      // C/min: latch compressor = 1
constexpr float COMP_OFF_SLOPE = 0.3f;      // C/min: latch compressor = 0
constexpr float NEAR_BOX_C = 1.0f;          // |fin - box| "coil at air temp"
constexpr uint32_t NEAR_BOX_DEFROST_MS = 120000; // defrost exit via fin ~ box
constexpr uint32_t NEAR_BOX_COMP_MS = 60000;     // relay open this long first
constexpr uint32_t NO_RESPONSE_MS = 600000;      // 10 min closed, no compressor

constexpr int CAL_MAX_POINTS = 12;
constexpr int CAL_STAB_N = 24;              // 24 x 5 s = 120 s stability window
constexpr float CAL_STAB_TEMP_C = 0.2f;
constexpr float CAL_STAB_OHMS_FRAC = 0.01f;
constexpr float CAL_MIN_SEP_C = 1.0f;
constexpr int CAL_MIN_POINTS = 4;
constexpr float CAL_MIN_SPAN_C = 8.0f;
constexpr float CAL_BETA_MIN = 2500.0f, CAL_BETA_MAX = 5500.0f;
constexpr float CAL_MAX_ERR_C = 1.0f;

// ---- settings (spec §2.11), persisted by the YAML ---------------------------
struct Settings {
    int coolerset = 4;       // C       2 .. 40
    int range = 2;           // C       0 .. 5
    int fin_cutoff = 0;      // C      -5 .. 5
    int fin_recover = 3;     // C       fin_cutoff+1 .. 10
    int settle = 10;         // min     2 .. 30
    int minofftime = 5;      // min     0 .. 30
    int minruntime = 180;    // s       0 .. 600
    int maxrun = 10;         // min     1 .. 60
    int dutypercent = 50;    // %       1 .. 100
    int sampleinterval = 3600; // s    10 .. 3600
};

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Clamp every field to its bounds. fin_cutoff is clamped first; fin_recover's
// floor then tracks it, so raising fin_cutoff bumps fin_recover up.
inline void clamp_settings(Settings& s) {
    s.coolerset = clampi(s.coolerset, 2, 40);
    s.range = clampi(s.range, 0, 5);
    s.fin_cutoff = clampi(s.fin_cutoff, -5, 5);
    s.fin_recover = clampi(s.fin_recover, s.fin_cutoff + 1, 10);
    s.settle = clampi(s.settle, 2, 30);
    s.minofftime = clampi(s.minofftime, 0, 30);
    s.minruntime = clampi(s.minruntime, 0, 600);
    s.maxrun = clampi(s.maxrun, 1, 60);
    s.dutypercent = clampi(s.dutypercent, 1, 100);
    s.sampleinterval = clampi(s.sampleinterval, 10, 3600);
}

// ---- fin thermistor calibration (spec §2.7), persisted by the YAML ---------
struct FinCal {
    float beta = 3950.0f;
    float r0 = 10000.0f;     // ohms at 25 C
    bool calibrated = false;
    float last_err = NAN;    // RMS residual of the last fit attempt, C
};

inline void fincal_reset(FinCal& c) { c = FinCal{}; }

// Divider: 3.3 V -> R_FIXED -> ADC -> thermistor -> GND.
inline float fin_ohms_from_volts(float v) {
    if (!(v > 0.0f) || !(v < FIN_VREF)) return NAN;
    return FIN_R_FIXED * v / (FIN_VREF - v);
}

inline float fin_c_from_ohms(float ohms, const FinCal& c) {
    if (!(ohms > 0.0f)) return NAN;
    float inv_t = 1.0f / T0_K + std::log(ohms / c.r0) / c.beta;
    return 1.0f / inv_t - 273.15f;
}

struct CalPoint { float ohms; float temp_c; };
struct CalFit { bool ok; float beta; float r0; float err_c; };

// Least-squares fit of 1/T = a + b*ln(R). B = 1/b, R0 from a at T0.
inline CalFit fit_beta(const CalPoint* p, int n) {
    CalFit f{false, NAN, NAN, NAN};
    if (n < 2) return f;
    double sx = 0, sy = 0;
    for (int i = 0; i < n; i++) { sx += std::log((double)p[i].ohms); sy += 1.0 / (p[i].temp_c + 273.15); }
    double mx = sx / n, my = sy / n, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
        double dx = std::log((double)p[i].ohms) - mx, dy = 1.0 / (p[i].temp_c + 273.15) - my;
        sxx += dx * dx; sxy += dx * dy;
    }
    if (sxx <= 0) return f;
    double b = sxy / sxx, a = my - b * mx;
    if (!(b > 0)) return f;
    double beta = 1.0 / b;
    double r0 = std::exp((1.0 / T0_K - a) * beta);
    double se = 0;
    for (int i = 0; i < n; i++) {
        double t = 1.0 / (a + b * std::log((double)p[i].ohms)) - 273.15;
        se += (t - p[i].temp_c) * (t - p[i].temp_c);
    }
    f.ok = true; f.beta = (float)beta; f.r0 = (float)r0; f.err_c = (float)std::sqrt(se / n);
    return f;
}

}  // namespace cooler
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd ~/Cooler/controller && cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `100% tests passed` (3 test cases).

- [ ] **Step 5: Commit**

```bash
cd ~/Cooler
git add controller/cooler_logic.h controller/tests
git commit -m "feat(controller): settings clamps, fin thermistor conversion, Beta fit

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The control step

Implements spec §2.1–§2.10 in one function. It is one task because modes, protections and the state they share can't be reviewed apart.

**Files:**
- Modify: `controller/cooler_logic.h` (insert before the final `}  // namespace cooler`)
- Modify: `controller/tests/harness.h` (replace whole file)
- Create: `controller/tests/test_step.cpp`

**Interfaces:**
- Consumes: everything from Task 1.
- Produces (namespace `cooler`):
  - `struct Inputs { bool box_valid; float box_c; float fin_volts; bool switch_closed; bool mqtt_connected; }`
  - `enum class Mode { Normal, Override, FinProxy, OverrideProxy, Blind }`, `enum class OverrideSrc { None, Switch, Link, Both }`, `enum class RunState { Idle, Cooling, Defrost, Wait, Rest }`, `enum class Led { Off, Solid, Slow, Fast }`
  - `const char* mode_str(Mode)`, `ovr_str(OverrideSrc)`, `state_str(RunState)`, which return the exact `/data` strings (`"normal"`, `"override-proxy"`, `"cooling"`, …)
  - `struct Outputs { bool relay; Led led; Mode mode; OverrideSrc override_src; RunState state; bool cool_call, defrost, sht_fault, fin_fault, link_lost, no_response; int compressor /* -1 null */; float fin_c, fin_ohms, fin_slope /* NAN = null */; uint32_t run_s, off_s, hold_s; bool cal_active; int cal_points; float cal_span; bool publish_now; }`
  - `struct LogicState` (opaque to callers; default-construct once)
  - `Outputs step(const Inputs&, const Settings&, FinCal&, LogicState&, uint32_t now_ms)`. `FinCal` is written when a calibration fit is accepted or rejected.
  - `void cal_start(LogicState&)`, `void cal_abort(LogicState&)`
- Produces (namespace `th`): `struct Rig { Settings s; FinCal cal; LogicState st; Inputs in; Outputs out; uint32_t now; box(c); box_dead(); fin(c); fin_open(); tick(); run(seconds); }`

- [ ] **Step 1: Replace `controller/tests/harness.h`**

```cpp
// Test helpers shared by the unit tests and the scenario sims.
#pragma once
#include "cooler_logic.h"

namespace th {

// ADC volts the divider produces for a thermistor at temp_c (Beta 3950, 10k).
inline float volts_for_c(float temp_c, float beta = 3950.0f, float r0 = 10000.0f) {
    float r = r0 * std::exp(beta * (1.0f / (temp_c + 273.15f) - 1.0f / cooler::T0_K));
    return cooler::FIN_VREF * r / (r + cooler::FIN_R_FIXED);
}

// Owns one controller instance and advances it one second per tick().
struct Rig {
    cooler::Settings s;
    cooler::FinCal cal;
    cooler::LogicState st;
    cooler::Inputs in;
    cooler::Outputs out;
    uint32_t now = 0;

    Rig() {
        in.box_valid = true; in.box_c = 4.0f;
        in.fin_volts = volts_for_c(4.0f);
        in.mqtt_connected = true;
    }
    void box(float c) { in.box_valid = true; in.box_c = c; }
    void box_dead() { in.box_valid = false; in.box_c = NAN; }
    void fin(float c) { in.fin_volts = volts_for_c(c); }
    void fin_open() { in.fin_volts = 3.29f; }
    const cooler::Outputs& tick() { out = cooler::step(in, s, cal, st, now); now += 1000; return out; }
    const cooler::Outputs& run(uint32_t seconds) { for (uint32_t i = 0; i < seconds; i++) tick(); return out; }
};

}  // namespace th
```

- [ ] **Step 2: Write the failing tests, `controller/tests/test_step.cpp`**

```cpp
#include <initializer_list>
#include "doctest/doctest.h"
#include "harness.h"

using namespace cooler;
using th::Rig;

TEST_CASE("boot: relay open, minofftime counted from boot") {
    Rig r; r.box(10.0f); r.fin(8.0f);
    r.run(5 * 60 - 1);
    CHECK_FALSE(r.out.relay);
    CHECK(r.out.state == RunState::Wait);
    CHECK(r.out.cool_call);
    r.run(2);
    CHECK(r.out.relay);
}

TEST_CASE("normal thermostat: strict thresholds and hold band") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f);
    r.box(6.0f); r.tick(); CHECK_FALSE(r.out.relay);       // 6 is not > 4+2
    r.box(6.1f); r.tick(); CHECK(r.out.relay);
    r.box(2.0f); r.tick(); CHECK(r.out.relay);             // 2 is not < 4-2
    r.box(1.9f); r.tick(); CHECK_FALSE(r.out.relay);
    r.box(4.0f); r.tick(); CHECK_FALSE(r.out.relay);       // held off in band
    CHECK(r.out.mode == Mode::Normal);
}

TEST_CASE("override: fixed 5 / 3 thermostat ignores coolerset/range") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.coolerset = 20; r.fin(8.0f);
    r.in.switch_closed = true;
    r.box(4.9f); r.tick(); CHECK_FALSE(r.out.relay);
    r.box(5.0f); r.tick(); CHECK(r.out.relay);
    CHECK(r.out.mode == Mode::Override);
    CHECK(r.out.override_src == OverrideSrc::Switch);
    r.box(3.1f); r.tick(); CHECK(r.out.relay);
    r.box(3.0f); r.tick(); CHECK_FALSE(r.out.relay);
}

TEST_CASE("override via link loss: 60 s debounce both ways") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f); r.box(4.0f);
    r.tick();
    r.in.mqtt_connected = false;
    r.run(59); CHECK(r.out.mode == Mode::Normal);
    r.run(2);  CHECK(r.out.mode == Mode::Override);
    CHECK(r.out.override_src == OverrideSrc::Link);
    r.in.switch_closed = true; r.tick(); CHECK(r.out.override_src == OverrideSrc::Both);
    r.in.switch_closed = false;
    r.in.mqtt_connected = true;
    r.run(59); CHECK(r.out.mode == Mode::Override);
    r.run(2);  CHECK(r.out.mode == Mode::Normal);
}

TEST_CASE("thermostat hold state carries across override -> normal") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f);
    r.in.switch_closed = true;
    r.box(5.0f); r.tick(); CHECK(r.out.relay);
    r.in.switch_closed = false;
    r.box(4.0f); r.tick();                 // in normal's band: hold -> still on
    CHECK(r.out.mode == Mode::Normal);
    CHECK(r.out.relay);
}

TEST_CASE("fin lockout: entry at cutoff, exit at recover, beats override and minruntime") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 600; r.in.switch_closed = true;
    r.box(8.0f); r.fin(5.0f); r.tick(); CHECK(r.out.relay);
    r.fin(0.0f); r.tick();
    CHECK_FALSE(r.out.relay);
    CHECK(r.out.defrost);
    CHECK(r.out.state == RunState::Defrost);
    CHECK(r.out.led == Led::Slow);
    r.fin(2.9f); r.tick(); CHECK(r.out.defrost);
    r.fin(3.0f); r.tick(); CHECK_FALSE(r.out.defrost);
    CHECK(r.out.relay);
}

TEST_CASE("defrost exits via fin ~ box when the box is colder than fin_recover") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0;
    r.box(8.0f); r.fin(2.0f); r.tick();
    r.fin(-1.0f); r.tick(); CHECK(r.out.defrost);
    r.box(2.0f); r.fin(1.5f);
    r.run(119); CHECK(r.out.defrost);
    r.run(2);   CHECK_FALSE(r.out.defrost);
}

TEST_CASE("defrost does not exit via fin ~ box while the SHT30 is faulted") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0;
    r.box(8.0f); r.fin(-1.0f); r.tick(); CHECK(r.out.defrost);
    r.in.box_valid = false; r.in.box_c = 1.5f;   // stale value, reading invalid
    r.fin(1.5f);
    r.run(400);
    CHECK(r.out.sht_fault);
    CHECK(r.out.defrost);
}

TEST_CASE("sht fault after 300 s, last demand held until then, auto-recovers") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.fin(8.0f);
    r.box(7.0f); r.tick(); CHECK(r.out.relay);
    r.box_dead();
    r.run(300); CHECK_FALSE(r.out.sht_fault); CHECK(r.out.relay);
    r.run(2);   CHECK(r.out.sht_fault);
    CHECK(r.out.mode == Mode::FinProxy);
    CHECK(r.out.led == Led::Fast);
    r.box(4.0f); r.tick(); CHECK_FALSE(r.out.sht_fault);
}

TEST_CASE("fin fault debounce 10 s, backstop only while faulted") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f); r.fin(8.0f);
    r.tick(); CHECK(r.out.relay);
    r.fin_open();
    r.run(9);  CHECK_FALSE(r.out.fin_fault);
    r.run(2);  CHECK(r.out.fin_fault);
    CHECK(r.out.compressor == -1);
    // maxrun 10, duty 50: on for 5 min of each 10
    int on = 0;
    for (int i = 0; i < 1200; i++) { r.tick(); if (r.out.relay) on++; }
    CHECK(on == doctest::Approx(600).epsilon(0.01));
    r.fin(8.0f); r.run(11);
    CHECK_FALSE(r.out.fin_fault);
    r.run(600);
    CHECK(r.out.relay);                    // no backstop rest once healthy
}

TEST_CASE("backstop: minruntime longer than the duty portion forfeits the window") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 300; r.s.maxrun = 10; r.s.dutypercent = 10;
    r.box(8.0f); r.fin_open(); r.run(11); REQUIRE(r.out.fin_fault);
    int on = 0;
    for (int i = 0; i < 600; i++) { r.tick(); if (r.out.relay) on++; }
    CHECK(on >= 299);
    CHECK(on <= 301);
}

TEST_CASE("blind mode when both sensors fail") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0;
    r.box_dead(); r.fin_open();
    r.run(302);
    CHECK(r.out.mode == Mode::Blind);
    CHECK(r.out.cool_call);
}

TEST_CASE("fin-proxy: rest for settle, cool on settled fin >= on, stop at cutoff or maxrun") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 10; r.s.maxrun = 10;
    r.fin(7.0f);
    r.box(4.0f); r.tick();
    r.box_dead(); r.run(301);
    REQUIRE(r.out.mode == Mode::FinProxy);
    r.run(10 * 60 - 310);
    CHECK_FALSE(r.out.relay);
    CHECK(r.out.state == RunState::Rest);
    r.run(20);
    CHECK(r.out.relay);                          // 7 >= 4+2 after settle
    r.run(10 * 60);
    CHECK_FALSE(r.out.relay);                    // maxrun reached
    CHECK(r.out.state == RunState::Rest);
}

TEST_CASE("override-proxy uses the fixed 5 C on-threshold") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 20;
    r.in.switch_closed = true; r.fin(5.5f);
    r.box_dead(); r.run(302);
    REQUIRE(r.out.mode == Mode::OverrideProxy);
    r.run(130);
    CHECK(r.out.relay);
}

TEST_CASE("compressor inference: latches on falling slope, holds flat, off on rising") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(10.0f);
    float f = 10.0f; r.fin(f);
    r.run(60); CHECK(r.out.compressor == 0);
    for (int i = 0; i < 60; i++) { f -= 1.0f / 60; r.fin(f); r.tick(); }   // -1 C/min
    CHECK(r.out.compressor == 1);
    r.run(120); CHECK(r.out.compressor == 1);                              // flat & cold
    for (int i = 0; i < 60; i++) { f += 0.5f / 60; r.fin(f); r.tick(); }   // +0.5 C/min
    CHECK(r.out.compressor == 0);
}

TEST_CASE("compressor latches off via fin ~ box once the relay has been open 60 s") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(4.0f); r.fin(4.0f);
    r.st.compressor = 1; r.st.init = false;
    r.tick(); r.st.compressor = 1;
    r.run(59); CHECK(r.out.compressor == 1);
    r.run(2);  CHECK(r.out.compressor == 0);
}

TEST_CASE("no_response after 10 min closed with no compressor, clears on relay open") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(8.0f); r.fin(8.0f);
    r.tick(); REQUIRE(r.out.relay);
    r.run(598); CHECK_FALSE(r.out.no_response);
    r.run(2);   CHECK(r.out.no_response);
    CHECK(r.out.led == Led::Fast);
    r.box(1.0f); r.tick();
    CHECK_FALSE(r.out.relay);
    CHECK_FALSE(r.out.no_response);
}

TEST_CASE("publish_now fires on a state change and not on a quiet tick") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(4.0f); r.fin(4.0f);
    r.tick(); CHECK(r.out.publish_now);
    r.tick(); CHECK_FALSE(r.out.publish_now);
    r.box(7.0f); r.tick(); CHECK(r.out.publish_now);
}

TEST_CASE("calibration: collects settled points and accepts a good fit") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    // Real probe: Beta 3600, R0 12k. Controller starts on defaults (3950 / 10k).
    auto set_both = [&](float c) {
        r.box(c);
        float rr = 12000.0f * std::exp(3600.0f * (1.0f / (c + 273.15f) - 1.0f / T0_K));
        r.in.fin_volts = FIN_VREF * rr / (rr + FIN_R_FIXED);
    };
    set_both(11.0f); r.tick();
    cal_start(r.st);
    for (float c : {11.0f, 14.0f, 17.0f, 20.0f}) { set_both(c); r.run(10 * 60); }
    CHECK_FALSE(r.out.cal_active);
    CHECK(r.cal.calibrated);
    CHECK(r.cal.beta == doctest::Approx(3600.0f).epsilon(0.01));
    CHECK(r.cal.last_err < 0.2f);
}

TEST_CASE("calibration rejects unstable and too-close points") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    r.box(10.0f); r.fin(10.0f); r.tick();
    cal_start(r.st);
    for (int i = 0; i < 600; i++) { r.box(10.0f + (i % 2) * 0.5f); r.fin(10.0f); r.tick(); }
    CHECK(r.out.cal_points == 0);                          // box wobbling 0.5 C
    r.box(10.0f); r.run(600);
    CHECK(r.out.cal_points == 1);
    r.box(10.5f); r.fin(10.5f); r.run(600);
    CHECK(r.out.cal_points == 1);                          // within 1 C of a point
}

TEST_CASE("calibration fit rejected keeps previous values") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.coolerset = 30;
    r.tick(); cal_start(r.st);
    // fin ohms stay constant while box moves: nonsense curve
    for (float c : {20.0f, 17.0f, 14.0f, 11.0f}) { r.box(c); r.fin(15.0f); r.run(600); }
    CHECK_FALSE(r.out.cal_active);
    CHECK_FALSE(r.cal.calibrated);
    CHECK(r.cal.beta == 3950.0f);
}

TEST_CASE("millis() wrap: minofftime and thermostat survive the 49.7-day rollover") {
    Rig r; r.now = 0xFFFFFFFFu - 60000u;       // wraps one minute after boot
    r.box(10.0f); r.fin(8.0f);
    r.run(5 * 60 - 1);
    CHECK_FALSE(r.out.relay);                  // minofftime still counted across the wrap
    r.run(2);
    CHECK(r.out.relay);
    r.box(1.0f); r.run(200);
    CHECK_FALSE(r.out.relay);
}

TEST_CASE("settings changed mid-run apply on the next tick") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.s.settle = 2; r.s.maxrun = 30;
    r.fin(7.0f); r.box(4.0f); r.tick();
    r.box_dead(); r.run(301 + 120);
    REQUIRE(r.out.relay);                      // fin-proxy COOL, 30 min allowed
    r.run(5 * 60);
    CHECK(r.out.relay);
    r.s.maxrun = 5;                            // shrink below time already run
    r.tick();
    CHECK_FALSE(r.out.relay);
}

TEST_CASE("fin ADC noise does not latch the compressor on") {
    Rig r; r.s.minofftime = 0; r.s.minruntime = 0; r.box(4.0f);
    for (int i = 0; i < 600; i++) { r.fin(4.0f + ((i / 5) % 2 ? 0.15f : -0.15f)); r.tick(); }
    CHECK(r.out.compressor == 0);
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd ~/Cooler/controller && cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: compile errors, `'Inputs' in namespace 'cooler' does not name a type` (and similar).

- [ ] **Step 4: Implement. Insert this block into `controller/cooler_logic.h` immediately before the final line `}  // namespace cooler`**

```cpp
// ---- per-tick inputs / outputs ---------------------------------------------
struct Inputs {
    bool box_valid = false;   // SHT30 reading present this tick
    float box_c = NAN;
    float fin_volts = NAN;    // latest ADC reading, NAN if none
    bool switch_closed = false; // latching override switch, debounced
    bool mqtt_connected = false;
};

enum class Mode : uint8_t { Normal, Override, FinProxy, OverrideProxy, Blind };
enum class OverrideSrc : uint8_t { None, Switch, Link, Both };
enum class RunState : uint8_t { Idle, Cooling, Defrost, Wait, Rest };
enum class Led : uint8_t { Off, Solid, Slow, Fast };

inline const char* mode_str(Mode m) {
    switch (m) {
        case Mode::Normal: return "normal";
        case Mode::Override: return "override";
        case Mode::FinProxy: return "fin-proxy";
        case Mode::OverrideProxy: return "override-proxy";
        default: return "blind";
    }
}
inline const char* ovr_str(OverrideSrc o) {
    switch (o) {
        case OverrideSrc::Switch: return "switch";
        case OverrideSrc::Link: return "link";
        case OverrideSrc::Both: return "both";
        default: return "none";
    }
}
inline const char* state_str(RunState s) {
    switch (s) {
        case RunState::Cooling: return "cooling";
        case RunState::Defrost: return "defrost";
        case RunState::Wait: return "wait";
        case RunState::Rest: return "rest";
        default: return "idle";
    }
}

struct Outputs {
    bool relay = false;
    Led led = Led::Off;
    Mode mode = Mode::Normal;
    OverrideSrc override_src = OverrideSrc::None;
    RunState state = RunState::Idle;
    bool cool_call = false;
    bool defrost = false;
    bool sht_fault = false;
    bool fin_fault = false;
    bool link_lost = false;
    bool no_response = false;
    int compressor = -1;      // -1 = null (fin fault), 0, 1
    float fin_c = NAN;        // NAN = null
    float fin_ohms = NAN;
    float fin_slope = NAN;    // C/min
    uint32_t run_s = 0, off_s = 0, hold_s = 0;
    bool cal_active = false;
    int cal_points = 0;
    float cal_span = 0.0f;
    bool publish_now = false; // a tracked field changed this tick
};

// ---- persistent-across-ticks state -----------------------------------------
struct LogicState {
    bool init = false;

    bool relay = false;
    uint32_t relay_changed_ms = 0;   // treated as "opened at boot"

    uint32_t sht_last_good_ms = 0;
    bool sht_fault = false;

    bool fin_fault = false;
    bool fin_last_valid = true;
    uint32_t fin_valid_changed_ms = 0;

    bool link_lost = false;
    bool link_last = false;
    uint32_t link_changed_ms = 0;

    bool demand = false;             // thermostat hold (normal + override share it)

    Mode last_mode = Mode::Normal;
    bool proxy_cool = false;         // fin-proxy phase: false REST, true COOL

    bool defrost = false;
    bool near_box = false;
    uint32_t near_box_since = 0;

    bool duty_active = false;
    uint32_t duty_start_ms = 0;

    float slope_ring[SLOPE_N] = {};
    int slope_n = 0, slope_head = 0;
    uint32_t last_sample_ms = 0;
    bool sampled_once = false;

    int compressor = 0;              // -1 null, 0, 1
    bool nr_timing = false;
    uint32_t nr_since = 0;
    bool no_response = false;

    bool cal_active = false;
    CalPoint cal_pts[CAL_MAX_POINTS] = {};
    int cal_n = 0;
    float stab_box[CAL_STAB_N] = {}, stab_ohms[CAL_STAB_N] = {};
    int stab_n = 0, stab_head = 0;

    // change detection for publish_now
    uint32_t sig = 0;
};

inline void cal_start(LogicState& st) { st.cal_active = true; st.cal_n = 0; st.stab_n = 0; st.stab_head = 0; }
inline void cal_abort(LogicState& st) { st.cal_active = false; st.cal_n = 0; }

// Least-squares slope of the ring, in C per minute. Samples are 5 s apart.
inline float ring_slope(const LogicState& st) {
    int n = st.slope_n;
    if (n < SLOPE_MIN_N) return NAN;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
        int idx = (st.slope_head - n + i + SLOPE_N) % SLOPE_N;   // oldest first
        double x = i * (FIN_SAMPLE_MS / 60000.0), y = st.slope_ring[idx];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double d = n * sxx - sx * sx;
    return d == 0 ? NAN : (float)((n * sxy - sx * sy) / d);
}

inline uint32_t sec_left(uint32_t elapsed_ms, uint32_t limit_ms) {
    return elapsed_ms >= limit_ms ? 0 : (limit_ms - elapsed_ms + 999) / 1000;
}

// One control tick. Call once a second (and again after any /cmd).
inline Outputs step(const Inputs& in, const Settings& s, FinCal& cal, LogicState& st, uint32_t now) {
    Outputs o;
    if (!st.init) {
        st.init = true;
        st.relay = false;
        st.relay_changed_ms = now;       // minofftime counts from boot
        st.sht_last_good_ms = now;
        st.fin_valid_changed_ms = now;
        st.link_last = in.mqtt_connected;
        st.link_changed_ms = now;
        st.last_sample_ms = now;
        st.last_mode = Mode::Normal;
    }
    const uint32_t since_relay = now - st.relay_changed_ms;

    // ---- faults (§2.1) ----
    if (in.box_valid) { st.sht_last_good_ms = now; st.sht_fault = false; }
    else if (now - st.sht_last_good_ms > SHT_FAULT_MS) { st.sht_fault = true; }

    float ohms = fin_ohms_from_volts(in.fin_volts);
    bool fin_valid = in.fin_volts >= FIN_V_MIN && in.fin_volts <= FIN_V_MAX &&
                     ohms >= FIN_OHMS_MIN && ohms <= FIN_OHMS_MAX;
    if (fin_valid != st.fin_last_valid) { st.fin_last_valid = fin_valid; st.fin_valid_changed_ms = now; }
    if (now - st.fin_valid_changed_ms >= FIN_DEBOUNCE_MS) st.fin_fault = !fin_valid;
    const bool fin_ok = fin_valid && !st.fin_fault;   // usable this tick
    const float fin = fin_ok ? fin_c_from_ohms(ohms, cal) : NAN;
    const bool box_ok = in.box_valid && !st.sht_fault;

    if (in.mqtt_connected != st.link_last) { st.link_last = in.mqtt_connected; st.link_changed_ms = now; }
    if (now - st.link_changed_ms >= LINK_DEBOUNCE_MS) st.link_lost = !in.mqtt_connected;

    // ---- 5 s fin sampling: slope ring + calibration stability ring ----
    bool sampled = false;
    if (!st.sampled_once || now - st.last_sample_ms >= FIN_SAMPLE_MS) {
        st.sampled_once = true;
        st.last_sample_ms = now;
        sampled = true;
        if (fin_ok) {
            st.slope_ring[st.slope_head] = fin;
            st.slope_head = (st.slope_head + 1) % SLOPE_N;
            if (st.slope_n < SLOPE_N) st.slope_n++;
        } else {
            st.slope_n = 0;
        }
    }
    const float slope = fin_ok ? ring_slope(st) : NAN;

    // ---- compressor inference (§2.8) ----
    if (!fin_ok) {
        st.compressor = -1;
    } else {
        if (st.compressor < 0) st.compressor = 0;
        if (!std::isnan(slope) && slope <= COMP_ON_SLOPE) st.compressor = 1;
        else if (!std::isnan(slope) && slope >= COMP_OFF_SLOPE) st.compressor = 0;
        else if (!st.relay && since_relay >= NEAR_BOX_COMP_MS && box_ok &&
                 std::fabs(fin - in.box_c) <= NEAR_BOX_C) st.compressor = 0;
    }

    // ---- mode (§2.2) ----
    const bool sw = in.switch_closed, ln = st.link_lost;
    const bool ovr = sw || ln;
    Mode mode;
    if (st.sht_fault && st.fin_fault) mode = Mode::Blind;
    else if (ovr) mode = st.sht_fault ? Mode::OverrideProxy : Mode::Override;
    else mode = st.sht_fault ? Mode::FinProxy : Mode::Normal;
    const bool proxy = mode == Mode::FinProxy || mode == Mode::OverrideProxy;
    const bool was_proxy = st.last_mode == Mode::FinProxy || st.last_mode == Mode::OverrideProxy;
    if (proxy && !was_proxy) st.proxy_cool = false;
    st.last_mode = mode;

    const float on_c = (mode == Mode::Override || mode == Mode::OverrideProxy)
                           ? OVR_ON_C : (float)(s.coolerset + s.range);
    const float off_c = (mode == Mode::Override || mode == Mode::OverrideProxy)
                            ? OVR_OFF_C : (float)(s.coolerset - s.range);

    // ---- demand (§2.2, §2.3) ----
    bool demand = false, settle_pending = false;
    const uint32_t settle_ms = (uint32_t)s.settle * 60000u;
    if (mode == Mode::Normal || mode == Mode::Override) {
        if (in.box_valid) {
            if (mode == Mode::Normal) {
                if (in.box_c > on_c) st.demand = true;
                if (in.box_c < off_c) st.demand = false;
            } else {
                if (in.box_c >= on_c) st.demand = true;
                if (in.box_c <= off_c) st.demand = false;
            }
        }
        demand = st.demand;
    } else if (proxy) {
        if (st.proxy_cool) {
            bool ran_out = st.relay && since_relay >= (uint32_t)s.maxrun * 60000u;
            if ((fin_ok && fin <= (float)s.fin_cutoff) || ran_out) st.proxy_cool = false;
        } else if (!st.relay) {
            if (since_relay < settle_ms) settle_pending = true;
            else if (fin_ok && fin >= on_c) st.proxy_cool = true;
        }
        demand = st.proxy_cool;
    } else {  // Blind
        demand = true;
    }

    // ---- protections (§2.4) ----
    // 1. fin lockout
    if (st.fin_fault) {
        st.defrost = false;
    } else if (fin_ok) {
        if (!st.defrost && fin <= (float)s.fin_cutoff) { st.defrost = true; st.near_box = false; }
        if (st.defrost) {
            bool near = box_ok && std::fabs(fin - in.box_c) <= NEAR_BOX_C;
            if (near && !st.near_box) st.near_box_since = now;
            st.near_box = near;
            if (fin >= (float)s.fin_recover || (near && now - st.near_box_since >= NEAR_BOX_DEFROST_MS)) {
                st.defrost = false; st.near_box = false;
            }
        }
    }  // fin invalid but not yet faulted: hold defrost as-is

    bool want = demand && !st.defrost;
    uint32_t hold_s = 0;
    bool duty_rest = false, minoff_hold = false;

    // 2. timed-duty backstop, only while the fin sensor is faulted
    if (st.fin_fault) {
        if (!st.duty_active) { st.duty_active = true; st.duty_start_ms = now; }
        uint32_t win = (uint32_t)s.maxrun * 60000u;
        uint32_t pos = (now - st.duty_start_ms) % win;
        uint32_t on = (uint32_t)((uint64_t)win * (uint32_t)s.dutypercent / 100u);
        uint32_t minrun = (uint32_t)s.minruntime * 1000u;
        if (minrun > 0 && on > 0 && on < minrun) on = minrun;
        if (on > win) on = win;
        if (want && pos >= on) { want = false; duty_rest = true; hold_s = sec_left(pos, win); }
    } else {
        st.duty_active = false;
    }

    // 3. minruntime (defrost may break it)
    const uint32_t minrun_ms = (uint32_t)s.minruntime * 1000u;
    if (st.relay && !want && !st.defrost && since_relay < minrun_ms) want = true;

    // 4. minofftime (counted from boot)
    const uint32_t minoff_ms = (uint32_t)s.minofftime * 60000u;
    if (!st.relay && want && since_relay < minoff_ms) {
        want = false; minoff_hold = true; hold_s = sec_left(since_relay, minoff_ms);
    }

    if (want != st.relay) { st.relay = want; st.relay_changed_ms = now; }
    const uint32_t in_state_ms = now - st.relay_changed_ms;

    // ---- no-response (§2.9) ----
    if (fin_ok && st.relay && st.compressor == 0) {
        if (!st.nr_timing) { st.nr_timing = true; st.nr_since = now; }
        if (now - st.nr_since >= NO_RESPONSE_MS) st.no_response = true;
    } else {
        st.nr_timing = false;
        st.no_response = false;
    }

    // ---- calibration (§2.7) ----
    if (st.cal_active && sampled) {
        bool steady = !st.relay && in_state_ms >= settle_ms && st.compressor == 0 && box_ok && fin_ok;
        if (!steady) {
            st.stab_n = 0;
        } else {
            st.stab_box[st.stab_head] = in.box_c;
            st.stab_ohms[st.stab_head] = ohms;
            st.stab_head = (st.stab_head + 1) % CAL_STAB_N;
            if (st.stab_n < CAL_STAB_N) st.stab_n++;
            if (st.stab_n == CAL_STAB_N) {
                float bmin = st.stab_box[0], bmax = bmin, omin = st.stab_ohms[0], omax = omin;
                for (int i = 1; i < CAL_STAB_N; i++) {
                    bmin = std::fmin(bmin, st.stab_box[i]); bmax = std::fmax(bmax, st.stab_box[i]);
                    omin = std::fmin(omin, st.stab_ohms[i]); omax = std::fmax(omax, st.stab_ohms[i]);
                }
                bool stable = bmax - bmin <= CAL_STAB_TEMP_C && omax - omin <= CAL_STAB_OHMS_FRAC * omin;
                bool far = true;
                for (int i = 0; i < st.cal_n; i++)
                    if (std::fabs(st.cal_pts[i].temp_c - in.box_c) < CAL_MIN_SEP_C) far = false;
                if (stable && far && st.cal_n < CAL_MAX_POINTS) {
                    st.cal_pts[st.cal_n++] = CalPoint{ohms, in.box_c};
                }
            }
        }
        if (st.cal_n >= CAL_MIN_POINTS) {
            float lo = st.cal_pts[0].temp_c, hi = lo;
            for (int i = 1; i < st.cal_n; i++) { lo = std::fmin(lo, st.cal_pts[i].temp_c); hi = std::fmax(hi, st.cal_pts[i].temp_c); }
            if (hi - lo >= CAL_MIN_SPAN_C) {
                CalFit f = fit_beta(st.cal_pts, st.cal_n);
                cal.last_err = f.err_c;
                if (f.ok && f.beta >= CAL_BETA_MIN && f.beta <= CAL_BETA_MAX && f.err_c <= CAL_MAX_ERR_C) {
                    cal.beta = f.beta; cal.r0 = f.r0; cal.calibrated = true;
                }
                st.cal_active = false;
            }
        }
    }

    // ---- run state, hold_s (§2.6) ----
    RunState rs;
    if (st.relay) {
        rs = RunState::Cooling;
        hold_s = sec_left(in_state_ms, minrun_ms);
    } else if (st.defrost) {
        rs = RunState::Defrost; hold_s = 0;
    } else if (minoff_hold) {
        rs = RunState::Wait;
    } else if (duty_rest) {
        rs = RunState::Rest;
    } else if (settle_pending) {
        rs = RunState::Rest; hold_s = sec_left(in_state_ms, settle_ms);
    } else {
        rs = RunState::Idle; hold_s = 0;
    }

    // ---- outputs ----
    o.relay = st.relay;
    o.mode = mode;
    o.override_src = sw && ln ? OverrideSrc::Both : sw ? OverrideSrc::Switch : ln ? OverrideSrc::Link : OverrideSrc::None;
    o.state = rs;
    o.cool_call = demand;
    o.defrost = st.defrost;
    o.sht_fault = st.sht_fault;
    o.fin_fault = st.fin_fault;
    o.link_lost = st.link_lost;
    o.no_response = st.no_response;
    o.compressor = st.compressor;
    o.fin_c = fin;
    o.fin_ohms = fin_ok ? ohms : NAN;
    o.fin_slope = slope;
    o.run_s = st.relay ? in_state_ms / 1000 : 0;
    o.off_s = st.relay ? 0 : in_state_ms / 1000;
    o.hold_s = hold_s;
    o.cal_active = st.cal_active;
    o.cal_points = st.cal_n;
    {
        float lo = 0, hi = 0;
        for (int i = 0; i < st.cal_n; i++) {
            float t = st.cal_pts[i].temp_c;
            if (i == 0 || t < lo) lo = t;
            if (i == 0 || t > hi) hi = t;
        }
        o.cal_span = hi - lo;
    }
    if (o.sht_fault || o.fin_fault || o.no_response) o.led = Led::Fast;
    else if (o.defrost) o.led = Led::Slow;
    else if (o.relay) o.led = Led::Solid;
    else o.led = Led::Off;

    uint32_t sig = (uint32_t)o.relay | (uint32_t)o.state << 1 | (uint32_t)o.mode << 4 |
                   (uint32_t)o.override_src << 7 | (uint32_t)o.sht_fault << 9 |
                   (uint32_t)o.fin_fault << 10 | (uint32_t)o.defrost << 11 |
                   (uint32_t)(o.compressor + 1) << 12 | (uint32_t)o.no_response << 14 |
                   (uint32_t)o.cal_active << 15 | (uint32_t)o.cal_points << 16;
    o.publish_now = sig != st.sig;
    st.sig = sig;
    return o;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cd ~/Cooler/controller && cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `100% tests passed` (27 test cases).

- [ ] **Step 6: Commit**

```bash
cd ~/Cooler
git add controller/cooler_logic.h controller/tests
git commit -m "feat(controller): control step -- modes, override, defrost, backstop, fin-proxy, inference, calibration

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Scenario simulations

Closed-loop, multi-hour runs against a thermal model (spec §6.2). Tuning note: the model's coil sits 10 °C below the box with about 5 min of thermal mass. With a harsher coil (14 °C, 2 min) the box plateaus near 8 °C under 0 °C lockout, the same plateau the v3 history shows. If the real unit behaves like that, the fix is tuning `fin_cutoff` during installation (Task 6), not changing this code.

**Files:**
- Create: `controller/tests/plant.h`, `controller/tests/test_scenarios.cpp`

**Interfaces:**
- Consumes: `th::Rig`, `th::volts_for_c`, `cooler::*` from Task 2.
- Produces: `th::Plant { float box, fin, ambient; bool comp; uint32_t restart_delay_ms, min_on_ms; bool unplugged; void tick(bool relay, uint32_t now_ms); }`

- [ ] **Step 1: Create `controller/tests/plant.h`**

```cpp
// Crude walk-in + Frigidaire model for scenario sims. One tick = 1 s.
//
//  - The AC turns the controller's relay request into compressor state with
//    its own restart delay and minimum on-time.
//  - A running compressor pulls the coil toward (box - 10 C); the coil ices
//    below 0 C in reality -- the sims assert the controller never lets the
//    relay stay closed there.
//  - With the compressor off, the continuous fan pulls the coil to box temp.
//  - The box leaks toward ambient and is cooled by the coil.
#pragma once
#include <cstdint>

namespace th {

struct Plant {
    float box = 20.0f, fin = 20.0f, ambient = 22.0f;
    bool comp = false;
    uint32_t comp_changed_ms = 0;
    uint32_t restart_delay_ms = 180000;
    uint32_t min_on_ms = 0;
    bool unplugged = false;

    static constexpr float COIL_DT = 10.0f;       // coil runs this far below box
    static constexpr float TAU_COIL_ON = 300.0f;  // s, coil + ice thermal mass
    static constexpr float TAU_COIL_OFF = 60.0f;  // s
    static constexpr float TAU_LEAK = 72000.0f;   // s, box -> ambient
    static constexpr float TAU_COOL = 3000.0f;    // s, coil -> box

    void tick(bool relay, uint32_t now) {
        if (unplugged) {
            comp = false;
        } else if (!comp && relay && now - comp_changed_ms >= restart_delay_ms) {
            comp = true; comp_changed_ms = now;
        } else if (comp && !relay && now - comp_changed_ms >= min_on_ms) {
            comp = false; comp_changed_ms = now;
        }
        float target = comp ? box - COIL_DT : box;
        fin += (target - fin) / (comp ? TAU_COIL_ON : TAU_COIL_OFF);
        float cool = comp ? (box - fin) / TAU_COOL : 0.0f;
        box += (ambient - box) / TAU_LEAK - cool;
    }
};

}  // namespace th
```

- [ ] **Step 2: Create `controller/tests/test_scenarios.cpp`**

```cpp
#include "doctest/doctest.h"
#include "harness.h"
#include "plant.h"

using namespace cooler;

namespace {

// Runs controller + plant together, checking the invariants every tick.
struct Sim {
    th::Rig r;
    th::Plant p;
    bool sht_dead = false, fin_dead = false;
    uint32_t prev_change = 0;
    bool prev_relay = false;
    int ticks_closed_below_cutoff = 0;   // consecutive
    int max_closed_below_cutoff = 0;
    bool minoff_violated = false, minrun_violated = false;
    uint32_t relay_on_ticks = 0, ticks = 0;

    Sim() { r.s.coolerset = 4; r.s.range = 2; }

    void tick() {
        if (sht_dead) r.box_dead(); else r.box(p.box);
        if (fin_dead) r.fin_open(); else r.fin(p.fin);
        uint32_t now = r.now;
        const Outputs& o = r.tick();
        if (o.relay != prev_relay) {
            uint32_t held = now - prev_change;
            if (o.relay && held < (uint32_t)r.s.minofftime * 60000u) minoff_violated = true;
            if (!o.relay && !o.defrost && held < (uint32_t)r.s.minruntime * 1000u) minrun_violated = true;
            prev_relay = o.relay; prev_change = now;
        }
        if (o.relay && !fin_dead && p.fin <= (float)r.s.fin_cutoff) ticks_closed_below_cutoff++;
        else ticks_closed_below_cutoff = 0;
        if (ticks_closed_below_cutoff > max_closed_below_cutoff) max_closed_below_cutoff = ticks_closed_below_cutoff;
        if (o.relay) relay_on_ticks++;
        ticks++;
        p.tick(o.relay, now);
    }
    void run_h(float hours) { for (uint32_t i = 0; i < (uint32_t)(hours * 3600); i++) tick(); }
    // run, recording the box min/max over the period
    void run_h_track(float hours, float& lo, float& hi) {
        lo = 1e9f; hi = -1e9f;
        for (uint32_t i = 0; i < (uint32_t)(hours * 3600); i++) {
            tick();
            if (p.box < lo) lo = p.box;
            if (p.box > hi) hi = p.box;
        }
    }
};

}  // namespace

TEST_CASE("scenario: normal pull-down 20 -> 4 C converges and holds the band") {
    Sim s;
    float lo, hi;
    s.run_h(16);
    s.run_h_track(8, lo, hi);
    CHECK(hi <= 7.0f);
    CHECK(lo >= 1.0f);
    CHECK_FALSE(s.minoff_violated);
    CHECK_FALSE(s.minrun_violated);
    CHECK(s.max_closed_below_cutoff <= 1);
}

TEST_CASE("scenario: pull-down runs continuously until the fin first reaches cutoff") {
    Sim s;
    bool opened_before_cutoff = false, closed_once = false;
    for (int i = 0; i < 16 * 3600; i++) {
        s.tick();
        if (s.r.out.relay) closed_once = true;
        if (closed_once && !s.r.out.relay) {
            if (!s.r.out.defrost) opened_before_cutoff = true;
            break;
        }
    }
    CHECK(closed_once);
    CHECK_FALSE(opened_before_cutoff);
}

TEST_CASE("scenario: defrost never latches forever with the box below fin_recover") {
    Sim s; s.r.s.fin_recover = 10; s.r.s.coolerset = 2; s.r.s.range = 0;
    s.run_h(24);
    // box sits near 2 C, far below fin_recover 10: defrost must still cycle out
    int defrost_ticks = 0;
    for (int i = 0; i < 3600; i++) { s.tick(); if (s.r.out.defrost) defrost_ticks++; }
    CHECK(defrost_ticks < 3600);
    CHECK(s.max_closed_below_cutoff <= 1);
}

TEST_CASE("scenario: override switch 24 h holds 3..5 C plus AC lag, defrost still fires") {
    Sim s; s.r.in.switch_closed = true; s.r.s.coolerset = 20;   // ignored in override
    float lo, hi;
    s.run_h(16);
    bool saw_defrost = false;
    lo = 1e9f; hi = -1e9f;
    for (int i = 0; i < 8 * 3600; i++) {
        s.tick();
        if (s.r.out.defrost) saw_defrost = true;
        if (s.p.box < lo) lo = s.p.box;
        if (s.p.box > hi) hi = s.p.box;
    }
    CHECK(s.r.out.mode == Mode::Override);
    CHECK(hi <= 6.0f);
    CHECK(lo >= 2.0f);
    CHECK(saw_defrost);
    CHECK(s.max_closed_below_cutoff <= 1);
}

TEST_CASE("scenario: link lost 30 min then restored") {
    Sim s; s.run_h(12);
    s.r.in.mqtt_connected = false;
    for (int i = 0; i < 61; i++) s.tick();
    CHECK(s.r.out.mode == Mode::Override);
    for (int i = 0; i < 30 * 60; i++) s.tick();
    s.r.in.mqtt_connected = true;
    for (int i = 0; i < 59; i++) s.tick();
    CHECK(s.r.out.mode == Mode::Override);
    for (int i = 0; i < 2; i++) s.tick();
    CHECK(s.r.out.mode == Mode::Normal);
}

TEST_CASE("scenario: SHT30 dies mid-run -> fin-proxy holds within +-3 C of the band") {
    Sim s; s.run_h(16);
    s.sht_dead = true;
    for (int i = 0; i < 301; i++) s.tick();
    CHECK(s.r.out.mode == Mode::FinProxy);
    float lo, hi;
    s.run_h_track(12, lo, hi);
    CHECK(hi <= 9.0f);
    CHECK(lo >= -1.0f);
    CHECK(s.max_closed_below_cutoff <= 1);
}

TEST_CASE("scenario: both sensors fail -> blind duty matches dutypercent") {
    Sim s; s.r.s.minruntime = 0; s.r.s.minofftime = 0;
    s.sht_dead = true; s.fin_dead = true;
    for (int i = 0; i < 400; i++) s.tick();
    REQUIRE(s.r.out.mode == Mode::Blind);
    s.relay_on_ticks = 0; s.ticks = 0;
    s.run_h(4);
    double duty = 100.0 * s.relay_on_ticks / s.ticks;
    CHECK(duty == doctest::Approx(50.0).epsilon(0.02));
}

TEST_CASE("scenario: AC ignores the relay -> no_response at 10 min") {
    Sim s; s.p.unplugged = true;
    // relay closes after minofftime (5 min) from boot, then 10 min more
    for (int i = 0; i < 5 * 60 + 2; i++) s.tick();
    REQUIRE(s.r.out.relay);
    for (int i = 0; i < 9 * 60; i++) s.tick();
    CHECK_FALSE(s.r.out.no_response);
    for (int i = 0; i < 61; i++) s.tick();
    CHECK(s.r.out.no_response);
}

TEST_CASE("scenario: compressor inference tracks the real compressor within 90 s") {
    Sim s; s.p.min_on_ms = 300000; s.r.s.minruntime = 180;
    uint32_t mismatch_run = 0, worst = 0;
    for (int i = 0; i < 16 * 3600; i++) {
        s.tick();
        int inferred = s.r.out.compressor;
        if (inferred >= 0 && (inferred == 1) != s.p.comp) mismatch_run++;
        else mismatch_run = 0;
        if (mismatch_run > worst) worst = mismatch_run;
    }
    CHECK(worst <= 90);
}

TEST_CASE("scenario: boot -> relay open for minofftime, override follows the switch") {
    Sim s; s.r.in.switch_closed = true;
    for (int i = 0; i < 5 * 60 - 1; i++) { s.tick(); REQUIRE_FALSE(s.r.out.relay); }
    CHECK(s.r.out.mode == Mode::Override);
    CHECK(s.r.out.override_src == OverrideSrc::Switch);
}
```

- [ ] **Step 3: Run**

Run: `cd ~/Cooler/controller && cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `100% tests passed` (37 test cases, well under a second). These tests exercise code from Task 2, so they pass immediately. To prove they can fail, run Step 4.

- [ ] **Step 4: Mutation check (do not commit)**

```bash
cd ~/Cooler/controller
cp cooler_logic.h /tmp/cooler_logic.h.bak
sed -i 's|if (!st.defrost \&\& fin <= (float)s.fin_cutoff)|if (false)|' cooler_logic.h
cmake --build build -j && ./build/controller_tests 2>&1 | grep -c "TEST CASE"   # expect 8
cp /tmp/cooler_logic.h.bak cooler_logic.h
cmake --build build -j && ctest --test-dir build   # back to 100% passed
```

- [ ] **Step 5: Commit**

```bash
cd ~/Cooler
git add controller/tests/plant.h controller/tests/test_scenarios.cpp
git commit -m "test(controller): closed-loop scenario sims against a thermal plant model

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: ESPHome firmware

**Files:**
- Create: `controller/cooler_esphome.h`, `controller/configs3-single.yaml`, `controller/secrets.yaml.example`
- Create (git-ignored, local only): `controller/secrets.yaml`

**Interfaces:**
- Consumes: `cooler::step`, `cooler::clamp_settings`, `cooler::cal_start` / `cal_abort`, `cooler::fincal_reset`, `mode_str` / `ovr_str` / `state_str`, `Led`.
- Produces (namespace `cooler_glue`): `settings()`, `fincal()`, `logic()`, `out()` (references to the single instances), `load()`, `save_settings()`, `save_fincal()`.

**Why a glue header:** ESPHome writes `globals:` declarations into `main.cpp` *before* it `#include`s project headers, so `type: cooler::Settings` in `globals:` fails to compile (`'cooler' was not declared in this scope`). The instances therefore live as function-local statics in `cooler_esphome.h`, and persistence goes through `global_preferences->make_preference<T>()`, which is what `restore_value` uses internally.

- [ ] **Step 1: Create `controller/cooler_esphome.h`**

```cpp
// cooler_esphome.h -- ESPHome-only glue for cooler_logic.h.
//
// ESPHome emits `globals:` declarations *before* it includes project headers,
// so a global cannot have a type from cooler_logic.h. The controller's state
// therefore lives here as function-local statics, and Settings / FinCal are
// persisted through ESPHome's preferences API directly (flash writes are
// batched by ESPHome's flash_write_interval, default 60 s).
#pragma once
#include "esphome/core/preferences.h"
#include "cooler_logic.h"

namespace cooler_glue {

// Preference keys. Changing a struct's layout changes its size, the load
// fails, and the defaults are used -- which is the intended upgrade path.
constexpr uint32_t PREF_SETTINGS = 0xC0015E71u;
constexpr uint32_t PREF_FINCAL = 0xC0017CA1u;

inline cooler::Settings& settings() { static cooler::Settings v; return v; }
inline cooler::FinCal& fincal() { static cooler::FinCal v; return v; }
inline cooler::LogicState& logic() { static cooler::LogicState v; return v; }
inline cooler::Outputs& out() { static cooler::Outputs v; return v; }

inline esphome::ESPPreferenceObject& settings_pref() {
    static esphome::ESPPreferenceObject p =
        esphome::global_preferences->make_preference<cooler::Settings>(PREF_SETTINGS);
    return p;
}
inline esphome::ESPPreferenceObject& fincal_pref() {
    static esphome::ESPPreferenceObject p =
        esphome::global_preferences->make_preference<cooler::FinCal>(PREF_FINCAL);
    return p;
}

// Call once from on_boot. Restores, re-clamps (bounds may have changed
// since the values were stored), and writes back.
inline void load() {
    if (!settings_pref().load(&settings())) settings() = cooler::Settings{};
    cooler::clamp_settings(settings());
    settings_pref().save(&settings());
    if (!fincal_pref().load(&fincal())) fincal() = cooler::FinCal{};
}

inline void save_settings() { settings_pref().save(&settings()); }
inline void save_fincal() { fincal_pref().save(&fincal()); }

}  // namespace cooler_glue
```

- [ ] **Step 2: Create `controller/configs3-single.yaml`**

```yaml
# v4 single-relay walk-in cooler controller (ESP32-S3).
# Rules: ../RULES-v4.md. Design: ../docs/superpowers/specs/2026-09-26-single-relay-controller-design.md
#
# This file is wiring and I/O only. Every control decision lives in
# cooler_logic.h, which is unit-tested on the host (tests/).

substitutions:
  device_name: esp32-cooler
  ovr_on_c: "5.0"
  ovr_off_c: "3.0"

esphome:
  name: ${device_name}
  includes:
    - cooler_logic.h
    - cooler_esphome.h
  platformio_options:
    build_flags:
      - "-DCOOLER_OVR_ON_C=${ovr_on_c}f"
      - "-DCOOLER_OVR_OFF_C=${ovr_off_c}f"
  on_boot:
    priority: -100
    then:
      - switch.turn_off: relay
      - lambda: 'cooler_glue::load();'

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: arduino

globals:
  # Settings, FinCal and the logic state are NOT globals: see cooler_esphome.h.

  # ---- history (same semantics as v3 RULES.md), persisted ----
  - { id: temp_hist,    type: 'int[20]', restore_value: yes }
  - { id: hum_hist,     type: 'int[20]', restore_value: yes }
  - { id: hist_n,       type: int,      restore_value: yes, initial_value: '0' }
  - { id: hist_last_ts, type: uint32_t, restore_value: yes, initial_value: '0' }
  - { id: sample_ctr,   type: int,      restore_value: no,  initial_value: '0' }

  # one-time cleanup of the v3 per-unit topics (spec §2.10)
  - { id: g_v3_cleared, type: int, restore_value: yes, initial_value: '0' }

  # ---- runtime ----
  - { id: g_dirty,   type: int,                restore_value: no, initial_value: '1' }

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: ${device_name}
    password: !secret ap_password

captive_portal:

time:
  - platform: sntp
    id: sntp_time

logger:
  level: INFO
  logs:
    mqtt.client: ERROR

ota:
  platform: esphome
  password: !secret ota_password

mqtt:
  id: mqtt_client
  broker: !secret mqtt_broker
  port: 1883
  username: !secret mqtt_username
  password: !secret mqtt_password
  keepalive: 15s
  client_id: ${device_name}
  topic_prefix: "ha/${device_name}"
  discovery: true
  discovery_prefix: "ha"
  birth_message:
    topic: ha/${device_name}/availability
    payload: online
    retain: true
  will_message:
    topic: ha/${device_name}/availability
    payload: offline
    retain: true
  on_connect:
    - lambda: 'id(g_dirty) = 1;'
    - if:
        condition:
          lambda: 'return id(g_v3_cleared) == 0;'
        then:
          # Empty retained payloads delete the stale v3 /ac1 and /ac2.
          - mqtt.publish: { topic: "ha/${device_name}/ac1", payload: "", retain: true }
          - mqtt.publish: { topic: "ha/${device_name}/ac2", payload: "", retain: true }
          - lambda: 'id(g_v3_cleared) = 1;'
  on_json_message:
    - topic: ha/${device_name}/cmd
      then:
        - lambda: |-
            cooler::Settings &s = cooler_glue::settings();
            if (x["coolerset"].is<int>())      s.coolerset      = x["coolerset"].as<int>();
            if (x["range"].is<int>())          s.range          = x["range"].as<int>();
            if (x["fin_cutoff"].is<int>())     s.fin_cutoff     = x["fin_cutoff"].as<int>();
            if (x["fin_recover"].is<int>())    s.fin_recover    = x["fin_recover"].as<int>();
            if (x["settle"].is<int>())         s.settle         = x["settle"].as<int>();
            if (x["minofftime"].is<int>())     s.minofftime     = x["minofftime"].as<int>();
            if (x["minruntime"].is<int>())     s.minruntime     = x["minruntime"].as<int>();
            if (x["maxrun"].is<int>())         s.maxrun         = x["maxrun"].as<int>();
            if (x["dutypercent"].is<int>())    s.dutypercent    = x["dutypercent"].as<int>();
            if (x["sampleinterval"].is<int>()) s.sampleinterval = x["sampleinterval"].as<int>();
            cooler::clamp_settings(s);
            cooler_glue::save_settings();

            if (x["calibrate"].is<int>()) {
              if (x["calibrate"].as<int>() == 1) cooler::cal_start(cooler_glue::logic());
              else cooler::cal_abort(cooler_glue::logic());
            }
            if (x["fincal_reset"].is<int>() && x["fincal_reset"].as<int>() == 1) {
              cooler::fincal_reset(cooler_glue::fincal());
              cooler_glue::save_fincal();
            }
            if (x["clearhist"].is<int>() && x["clearhist"].as<int>() == 1) {
              id(hist_n) = 0; id(sample_ctr) = 0;
            }
            id(g_dirty) = 1;
        - script.execute: control

switch:
  - platform: gpio
    id: relay
    # Internal: no MQTT command topic. Only cooler_logic.h may drive the relay;
    # a remote toggle would bypass minofftime and the override thermostat.
    internal: true
    # Low-level-trigger module: pin LOW = relay closed. Inverted so ON = closed.
    pin: { number: GPIO5, inverted: true }
    restore_mode: ALWAYS_OFF

output:
  - platform: gpio
    id: status_led
    pin: GPIO7

binary_sensor:
  - platform: gpio
    id: override_sw
    name: "Override Switch"
    # Latching switch to GND. Closed = override on.
    pin:
      number: GPIO6
      inverted: true
      mode: { input: true, pullup: true }
    filters:
      - delayed_on_off: 50ms
    on_state:
      - script.execute: control

i2c:
  sda: GPIO8
  scl: GPIO9
  scan: true
  frequency: 100khz

sensor:
  - platform: sht3xd
    address: 0x44
    update_interval: 10s
    temperature: { id: sht30t, name: "Cooler Temperature" }
    humidity:    { id: sht30h, name: "Cooler Humidity" }

  - platform: adc
    id: fin_adc
    pin: GPIO4
    attenuation: 12db
    update_interval: 5s
    internal: true

script:
  - id: control
    mode: queued
    max_runs: 3
    then:
      - lambda: |-
          cooler::Inputs in;
          in.box_valid = !std::isnan(id(sht30t).state);
          in.box_c = id(sht30t).state;
          in.fin_volts = id(fin_adc).state;
          in.switch_closed = id(override_sw).state;
          in.mqtt_connected = id(mqtt_client).is_connected();

          cooler::FinCal &c = cooler_glue::fincal();
          const cooler::FinCal before = c;
          cooler::Outputs o = cooler::step(in, cooler_glue::settings(), c, cooler_glue::logic(), millis());
          cooler_glue::out() = o;

          if (c.calibrated != before.calibrated || c.beta != before.beta || c.r0 != before.r0 ||
              std::isnan(c.last_err) != std::isnan(before.last_err) ||
              (!std::isnan(c.last_err) && c.last_err != before.last_err)) {
            cooler_glue::save_fincal();
            id(g_dirty) = 1;
          }
          if (o.relay != id(relay).state) {
            if (o.relay) id(relay).turn_on(); else id(relay).turn_off();
          }
          if (o.publish_now) id(g_dirty) = 1;

  - id: publish_state
    mode: single
    then:
      - lambda: 'id(g_dirty) = 0;'
      - mqtt.publish_json:
          topic: "ha/${device_name}/data"
          retain: true
          payload: |-
            const cooler::Outputs &o = cooler_glue::out();
            auto num_or_null = [&](const char *k, float v) {
              if (std::isnan(v)) root[k] = nullptr; else root[k] = v;
            };
            root["v"] = 2;
            num_or_null("temp", id(sht30t).state);
            num_or_null("humidity", id(sht30h).state);
            num_or_null("fin_temp", o.fin_c);
            num_or_null("fin_ohms", o.fin_ohms);
            num_or_null("fin_slope", o.fin_slope);
            root["mode"]         = cooler::mode_str(o.mode);
            root["override_src"] = cooler::ovr_str(o.override_src);
            root["state"]        = cooler::state_str(o.state);
            root["relay"]        = o.relay ? 1 : 0;
            root["cool_call"]    = o.cool_call ? 1 : 0;
            root["defrost"]      = o.defrost ? 1 : 0;
            if (o.compressor < 0) root["compressor"] = nullptr; else root["compressor"] = o.compressor;
            root["sht_fault"]    = o.sht_fault ? 1 : 0;
            root["fin_fault"]    = o.fin_fault ? 1 : 0;
            root["no_response"]  = o.no_response ? 1 : 0;
            root["run_s"]        = o.run_s;
            root["off_s"]        = o.off_s;
            root["hold_s"]       = o.hold_s;

            const cooler::Settings &s = cooler_glue::settings();
            root["coolerset"]      = s.coolerset;
            root["range"]          = s.range;
            root["maxrun"]         = s.maxrun;
            root["dutypercent"]    = s.dutypercent;
            root["minofftime"]     = s.minofftime;
            root["minruntime"]     = s.minruntime;
            root["sampleinterval"] = s.sampleinterval;
            root["fin_cutoff"]     = s.fin_cutoff;
            root["fin_recover"]    = s.fin_recover;
            root["settle"]         = s.settle;

            const cooler::FinCal &c = cooler_glue::fincal();
            root["fin_cal"]  = c.calibrated ? 1 : 0;
            root["fin_beta"] = c.beta;
            root["fin_r0"]   = c.r0;
            num_or_null("fin_cal_err", c.last_err);
            root["cal_active"] = o.cal_active ? 1 : 0;
            root["cal_points"] = o.cal_points;
            root["cal_span"]   = o.cal_span;

            root["hist_n"]          = id(hist_n);
            root["hist_interval_s"] = s.sampleinterval;
            root["hist_last_ts"]    = id(hist_last_ts);
            JsonArray th = root.createNestedArray("temp_hist");
            JsonArray hh = root.createNestedArray("hum_hist");
            for (int i = 0; i < id(hist_n); i++) { th.add(id(temp_hist)[i]); hh.add(id(hum_hist)[i]); }

            root["uptime_s"] = (uint32_t)(millis() / 1000);

interval:
  # 1 s control tick + history sampling
  - interval: 1s
    then:
      - lambda: |-
          id(sample_ctr) = id(sample_ctr) + 1;
          if (id(sample_ctr) >= cooler_glue::settings().sampleinterval) {
            id(sample_ctr) = 0;
            float ht = id(sht30t).state, hh = id(sht30h).state;
            if (!std::isnan(ht) && !std::isnan(hh)) {
              int ti = (int) lroundf(ht), hi = (int) lroundf(hh);
              if (id(hist_n) < 20) {
                id(temp_hist)[id(hist_n)] = ti; id(hum_hist)[id(hist_n)] = hi;
                id(hist_n) = id(hist_n) + 1;
              } else {
                for (int i = 0; i < 19; i++) { id(temp_hist)[i] = id(temp_hist)[i + 1]; id(hum_hist)[i] = id(hum_hist)[i + 1]; }
                id(temp_hist)[19] = ti; id(hum_hist)[19] = hi;
              }
              id(hist_last_ts) = (uint32_t) id(sntp_time).now().timestamp;
              id(g_dirty) = 1;
            }
          }
      - script.execute: control
      - if:
          condition:
            lambda: 'return id(g_dirty) != 0 && id(mqtt_client).is_connected();'
          then:
            - script.execute: publish_state

  # status every 30 s regardless of change
  - interval: 30s
    then:
      - if:
          condition:
            lambda: 'return id(mqtt_client).is_connected();'
          then:
            - script.execute: publish_state

  # status LED: 125 ms phase clock gives 1 Hz and 4 Hz blinks
  - interval: 125ms
    then:
      - lambda: |-
          uint32_t ph = millis() / 125;
          bool on;
          switch (cooler_glue::out().led) {
            case cooler::Led::Solid: on = true; break;
            case cooler::Led::Slow:  on = (ph / 4) % 2 == 0; break;
            case cooler::Led::Fast:  on = ph % 2 == 0; break;
            default:                 on = false; break;
          }
          if (on) id(status_led).turn_on(); else id(status_led).turn_off();
```

- [ ] **Step 3: Create `controller/secrets.yaml.example`, then a real `controller/secrets.yaml`**

```yaml
# Copy to secrets.yaml (git-ignored) and fill in.
wifi_ssid: ""
wifi_password: ""
ap_password: ""
ota_password: ""
mqtt_broker: "mqtt.example.com"
mqtt_username: ""
mqtt_password: ""
```

Copy it to `controller/secrets.yaml` and fill in the real values. The WiFi and MQTT values are the ones in `configc32-dual-v3.yaml`, which the user already has. Never add `secrets.yaml` to git: `git check-ignore controller/secrets.yaml` must print the path.

- [ ] **Step 4: Validate the config**

Run: `cd ~/Cooler/controller && esphome config configs3-single.yaml | tail -1`
Expected: `INFO Configuration is valid!`

- [ ] **Step 5: Compile**

Run: `cd ~/Cooler/controller && esphome compile configs3-single.yaml 2>&1 | tail -3`
Expected: `[SUCCESS]`, with RAM about 13 % and flash about 55 %.

- [ ] **Step 6: Flash and check `/data` on the broker (needs the board on USB)**

```bash
cd ~/Cooler/controller
esphome run configs3-single.yaml          # choose the USB port the first time
# in another terminal (credentials from secrets.yaml):
mosquitto_sub -h <broker> -u <user> -P <pass> -v -t 'ha/esp32-cooler/#'
```

Expected within 30 s:
- `ha/esp32-cooler/availability online`;
- a `ha/esp32-cooler/data` payload with `"v":2`, `"relay":0`, `"state":"wait"` or `"idle"`, and `"mode":"normal"` (or `"override"` if the switch is closed);
- the retained `ha/esp32-cooler/ac1` and `/ac2` are gone. Check with `mosquitto_sub … -t 'ha/esp32-cooler/ac1' -W 3`: no output;
- no topic under `ha/esp32-cooler/switch/`. The relay is `internal: true`, so there must be no MQTT command topic that could toggle it.

If the board isn't available yet, mark this step blocked, move on, and do it at the start of Task 6.

- [ ] **Step 7: `/cmd` smoke test (Review Focus #5)**

```bash
P='mosquitto_pub -h <broker> -u <user> -P <pass> -t ha/esp32-cooler/cmd -m'
$P '{"coolerset":50}'          # -> /data coolerset 40 (clamped)
$P '{"fin_cutoff":5}'          # -> fin_cutoff 5, fin_recover bumped to 6
$P '{"coolerset":4.5}'         # -> ignored: coolerset stays 40
$P '{"coolerset":"4"}'         # -> ignored
$P '{"bogus":1}'               # -> ignored, still publishes
$P '{"coolerset":4,"fin_cutoff":0,"fin_recover":3}'   # restore defaults
```

Expected: each `/data` republish (within about 1 s) shows exactly the commented result.

- [ ] **Step 8: Commit**

```bash
cd ~/Cooler
git add controller/cooler_esphome.h controller/configs3-single.yaml controller/secrets.yaml.example
git status --short | grep secrets.yaml$ && echo "STOP: secrets.yaml is staged" || true
git commit -m "feat(controller): ESPHome firmware for ESP32-S3 single-relay controller

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Operator rules and spec sync

**Files:**
- Create: `RULES-v4.md`
- Modify: `docs/superpowers/specs/2026-09-26-single-relay-controller-design.md` (§5)

- [ ] **Step 1: Create `RULES-v4.md`**

````markdown
# Walk-In Cooler Controller v4 — Rules

One Frigidaire window AC cools the walk-in. The ESP32-S3 never switches
compressor power: one relay swaps the resistance on the AC's room-thermistor
connector (CN3) so the AC's own board sees a "warm" room (≈10 kΩ, cool) or a
"cold" room (35 kΩ, stop). The Frigidaire keeps its own compressor protection.

Implemented in `controller/configs3-single.yaml` + `controller/cooler_logic.h`.
Full design: `docs/superpowers/specs/2026-09-26-single-relay-controller-design.md`.
The v3 dual-AC rules remain in `RULES.md` for reference.

# Hardware
| Part | Pin | Notes |
|---|---|---|
| SHT30 box sensor | SDA 8 / SCL 9 | I2C 0x44 |
| Fin thermistor (the AC's original, moved into the coil fins) | GPIO4 | 3.3 V → 33 kΩ → GPIO4 → thermistor → GND |
| Relay IN1 (B00XAGT2OG, JD-VCC jumper removed) | GPIO5 | low = closed = "warm" = cool |
| Override switch (latching, to GND) | GPIO6 | closed = override |
| Status LED | GPIO7 | see below |

CN3: 35 kΩ always across the pins; 14 kΩ in series with relay COM/NO across
the same pins. Nothing on the CN3 side touches the ESP32.

**Frigidaire setup:** Cool mode, fan **continuous**, setpoint ≈22 °C.

# Modes
| Mode | When | Cooling decision |
|---|---|---|
| override | override switch closed, **or** broker/WiFi lost for 60 s | fixed: on at ≥ 5 °C, off at ≤ 3 °C (not settable) |
| override-proxy | override + SHT30 failed | fin-proxy cycle with the 5 / 3 °C thresholds |
| normal | otherwise, SHT30 healthy | on above `coolerset + range`, off below `coolerset − range` |
| fin-proxy | SHT30 failed (300 s no reading) | rest `settle` min, read the fin as box temp, cool if ≥ on-threshold until fin cutoff or `maxrun` |
| blind | SHT30 and fin sensor both failed | cool `dutypercent` % of every `maxrun` window |

Override ends 60 s after the switch is open and the broker is back.

# Protections (apply in every mode)
1. **Icing (defrost)** — fin ≤ `fin_cutoff` (0 °C): relay opens regardless of
   box temperature; the fan melts the ice. Ends at fin ≥ `fin_recover`
   (+3 °C) or when the fin has matched box temperature (±1 °C) for 2 min.
2. **Fin sensor failed** — timed duty (`dutypercent` of `maxrun`) replaces
   icing protection until the sensor recovers.
3. **`minruntime`** — relay stays closed at least this long (defrost may cut it).
4. **`minofftime`** — relay stays open at least this long, counted from boot.

There is no duty cycle during a pull-down: the AC runs flat out while the coil
stays above 0 °C, and defrost breaks start by themselves as the box gets cold.

# Settings (MQTT `/cmd`, persisted, clamped)
| Key | Unit | Bounds | Default |
|---|---|---|---|
| `coolerset` | °C | 2 .. 40 | 4 |
| `range` | °C | 0 .. 5 | 2 |
| `fin_cutoff` | °C | −5 .. 5 | 0 |
| `fin_recover` | °C | `fin_cutoff`+1 .. 10 | 3 |
| `settle` | min | 2 .. 30 | 10 |
| `minofftime` | min | 0 .. 30 | 5 |
| `minruntime` | s | 0 .. 600 | 180 |
| `maxrun` | min | 1 .. 60 | 10 |
| `dutypercent` | % | 1 .. 100 | 50 |
| `sampleinterval` | s | 10 .. 3600 | 3600 |

Actions: `{"calibrate":1}` / `{"calibrate":0}`, `{"fincal_reset":1}`,
`{"clearhist":1}`. Non-integer values are ignored.

# Status LED
fast blink = sensor fault or no-response · slow blink = defrost ·
solid = cooling requested · off = otherwise.

# MQTT
Prefix `ha/esp32-cooler`. `/data` (retained, `"v": 2`) every 30 s and on any
change; `/availability` online/offline (LWT); `/cmd` as above. `/ac1` and
`/ac2` no longer exist. Field list: spec §3.1.

# Fin calibration
Send `{"calibrate":1}` with the box warm at the start of a pull-down. The
controller collects (fin resistance, SHT30 temperature) points while the relay
has been open ≥ `settle` min and both readings are steady, and fits the
thermistor's Beta once it has ≥ 4 points spanning ≥ 8 °C. `fin_cal: 1` and
`fin_cal_err` < 0.5 °C in `/data` mean done.

# Install checklist
1. AC unplugged: CN3 network reads ≈35 kΩ (relay open) / ≈10 kΩ (closed); no
   continuity from either CN3 lead to ESP32 GND or 3.3 V.
2. Relay stays open through power-up, reset, OTA and a crash.
3. A 10 kΩ pot in place of the fin probe sweeps `fin_temp`; open and shorted
   raise `fin_fault` within ~10 s.
4. Override switch → `mode: override` at once; broker down → override after 60 s,
   back to normal 60 s after it returns.
5. **AC timing:** set `minofftime` 0 and `minruntime` 0; close the relay and time
   until `fin_slope` goes clearly negative (= AC restart delay); open it ~30 s
   after the compressor starts and time how long the fin keeps falling (= AC
   minimum on-time). Repeat 3×, take the maxima. Set `minofftime` ≥ restart
   delay, `minruntime` ≥ minimum on-time, and raise `fin_cutoff` by the fin's
   overshoot after the relay opened.
6. Confirm the AC cools on relay close and stops on open; watch one defrost and
   confirm the fan keeps running.
7. Run calibration on the first pull-down.
8. Unplug the AC with the relay closed: `no_response` fires after 10 min.
````

- [ ] **Step 2: Update spec §5 to match what was built**

In `docs/superpowers/specs/2026-09-26-single-relay-controller-design.md`, make these three edits:

1. In §5, replace the bullet
   `` - `Outputs step(const Inputs&, Settings&, LogicState&, uint32_t now_ms)` ``
   with
   `` - `Outputs step(const Inputs&, const Settings&, FinCal&, LogicState&, uint32_t now_ms)` (FinCal is updated by a finished calibration run) ``
2. In §5, replace the paragraph that begins `- The YAML includes it via` and ends `**The YAML contains no control decisions.**` with:

```markdown
- The YAML includes it plus `cooler_esphome.h` via `esphome: includes:`. ESPHome declares
  `globals:` before project headers, so the Settings / FinCal / LogicState / Outputs instances
  live in `cooler_esphome.h` as function-local statics, and Settings and FinCal persist via
  `global_preferences->make_preference<T>()`. A 1 s `interval:` runs the `control` script, which
  fills `Inputs` (including `id(mqtt_client).is_connected()`), calls `step()`, drives GPIO5, and
  flags `/data` for publishing. A 125 ms interval renders the LED pattern. The MQTT
  `on_json_message` handler updates Settings and calibration actions, then re-runs `control`.
  **The YAML contains no control decisions.**
```

3. In the §5 file tree, add the line `  cooler_esphome.h       ESPHome glue: state instances + preferences` under `cooler_logic.h`.

- [ ] **Step 3: Commit**

```bash
cd ~/Cooler
git add RULES-v4.md docs/superpowers/specs/2026-09-26-single-relay-controller-design.md
git commit -m "docs: v4 operator rules and spec sync for the ESPHome glue header

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Bench and installation (human, with hardware)

This is not code. Each item needs a person, a meter and the AC. Work through `RULES-v4.md` → *Install checklist* items 1–8, recording each result in `docs/install-log-v4.md` (create it). Commit the log at the end. In detail:

- [ ] **Step 1:** Checklist 1 (CN3 network ≈35 kΩ / ≈10 kΩ, isolation). Record the two readings.
- [ ] **Step 2:** Checklist 2 (relay open through power-up, reset, `esphome run` OTA, and a crash: temporarily add `- lambda: 'abort();'` behind a test button, or just pull power mid-run).
- [ ] **Step 3:** Checklist 3 (10 kΩ pot on the fin input; open and short → `fin_fault` in about 10 s, LED fast blink).
- [ ] **Step 4:** Checklist 4 (override switch; stop the broker or block it at the router → override after 60 s, back 60 s after restore).
- [ ] **Step 5:** Checklist 5 (AC restart delay and minimum on-time via `fin_slope`, 3 runs). Record the numbers, then set `minofftime`, `minruntime` and `fin_cutoff` over `/cmd`. If the observed running slope is much gentler than −0.5 °C/min or the resting slope gentler than +0.3 °C/min, change `COMP_ON_SLOPE` / `COMP_OFF_SLOPE` in `cooler_logic.h`, rerun the host tests, and reflash.
- [ ] **Step 6:** Checklist 6 (AC follows the relay; fan runs through a defrost).
- [ ] **Step 7:** Checklist 7 (`{"calibrate":1}` at the start of the first pull-down; `fin_cal: 1`, `fin_cal_err` < 0.5).
- [ ] **Step 8:** Checklist 8 (unplug the AC with the relay closed → `no_response` at 10 min).
- [ ] **Step 9: Commit the log**

```bash
cd ~/Cooler
git add docs/install-log-v4.md
git commit -m "docs: v4 bench and installation results

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
