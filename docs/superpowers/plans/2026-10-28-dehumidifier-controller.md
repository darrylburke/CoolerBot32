# Dehumidifier Controller Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A new ESPHome firmware on the v1 DevKitC board that switches a dehumidifier relay over MQTT (AUTO / ON / OFF / TIMER / CYCLE), plus a Cooler v4 change that computes and sends the AUTO want.

**Architecture:** Same split as the Cooler: a pure C++17 header (`dehumidifier/dehum_logic.h`) holds every decision and is host-tested with doctest; the YAML is wiring and I/O only. The Cooler gets a small, separate want function in `cooler_logic.h` and a separately persisted `dehum_rh`, so its existing settings record and `step()` are untouched.

**Tech Stack:** ESPHome 2025.2.0 (esp-idf framework, `esp32dev` board), C++17, CMake ≥ 3.20, doctest 2.4.11 via FetchContent, g++.

**Spec:** `docs/superpowers/specs/2026-10-28-dehumidifier-controller-design.md` (read it first; section refs below are to it).

## Global Constraints

- Dehumidifier relay: **GPIO16**, `restore_mode: ALWAYS_OFF`, `internal: true`. Spare channel: **GPIO23**, held off, `internal: true`. Never use GPIO12 or GPIO14.
- Broker: same host as the Cooler (`mqtt_broker` secret), port 8883, TLS with the same private CA block as `controller/cooler-v4.yaml`. `reboot_timeout: 0s`. `discovery: false`. Topic prefix `dehum`.
- Topics: `dehum/cmd` (JSON, not retained), `dehum/want` (JSON `{"want":0|1}`, not retained, QoS 0, Cooler only), `dehum/data` (retained, `"v": 1`, every 30 s and on change), `dehum/availability` (`online`/`offline`, retained, birth/LWT).
- Dehumidifier settings (persisted, clamped): `min_run` 0..60 min (10), `timer_h` 1..24 h (2), `cycle_on` max(1,`min_run`)..240 min (30), `cycle_off` 1..240 min (30). Mode is persisted too (default AUTO). Non-integer values ignored; unknown mode strings ignored.
- Timings: broker-loss lockout after **60 s** disconnected (only after a first connect); Cooler `want` expires after **90 s**; Cooler sends want on change and every **30 s**.
- Cooler `dehum_rh`: 30..95 %RH, default 65, hysteresis band **3** (on at ≥ `dehum_rh`, off at < `dehum_rh − 3`). Must be stored in its **own** preference so the existing `cooler::Settings` record keeps its size and the live tuned settings survive the flash.
- Never flash the live Cooler or edit the live broker (`ssh champion`) without the user's explicit go-ahead in this session.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. Clock wrap / late ticks: `step()` called with `now` crossing `UINT32_MAX`, or 5 s after the previous tick, must keep timers correct — test `"timers survive millis wrap and late ticks"` (Task 1).
2. Re-sending TIMER or CYCLE while already in it restarts the countdown with the message's new values — test `"resend restarts with new values"` (Task 1).
3. Raising `min_run` above the stored `cycle_on` bumps `cycle_on` (and a `cycle_on` below `min_run` in the same message is raised) — test `"min_run bumps cycle_on"` (Task 1).
4. A `want` received while in ON/TIMER/CYCLE is remembered and used if the user switches to AUTO within 90 s — test `"want remembered across modes"` (Task 1).
5. The Cooler keeps its tuned settings across the upgrade — bench step in Task 4 (cannot be host-tested: it depends on ESPHome's preference store).

---

### Task 1: Dehumidifier logic header and host tests

**Files:**
- Create: `dehumidifier/dehum_logic.h`
- Create: `dehumidifier/tests/CMakeLists.txt` (copy of `controller/tests/CMakeLists.txt` with project/target names `dehum_tests`; no harness)
- Create: `dehumidifier/tests/test_main.cpp` (`#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + include, as in `controller/tests/test_main.cpp`)
- Create: `dehumidifier/tests/test_dehum.cpp`
- Modify: `.gitignore` — add `/dehumidifier/build/` and `/dehumidifier/*.log`

**Interfaces (Produces, all in `namespace dehum`):**

```cpp
enum class Mode : uint8_t { Auto, On, Off, Timer, Cycle };
const char* mode_str(Mode);                       // "auto" "on" "off" "timer" "cycle"
std::optional<Mode> parse_mode(const char*);      // exact lowercase match, else nullopt

struct Settings {                                 // persisted as one record
    int min_run = 10, timer_h = 2, cycle_on = 30, cycle_off = 30;
    Mode mode = Mode::Auto;
};
void clamp_settings(Settings&);                   // min_run first, cycle_on floor = max(1, min_run)

struct Cmd { std::optional<int> min_run, timer_h, cycle_on, cycle_off; std::optional<Mode> mode; };
// Settings first (then clamp), then mode. A present mode always restarts
// TIMER/CYCLE (even if unchanged). Returns true if Settings changed (caller saves).
bool apply_cmd(const Cmd&, Settings&, struct State&);

void on_want(State&, bool want);                  // marks want fresh (age 0)

struct Inputs { bool mqtt_connected = false; };
struct Outputs {
    bool relay = false;
    Mode mode = Mode::Auto;
    int want = -1;                 // -1 = none within 90 s, else 0/1
    bool link_lost = false;
    uint32_t hold_s = 0;           // min_run seconds left while relay on, else 0
    uint32_t remain_s = 0;         // TIMER: left; CYCLE: left in phase; else 0
    int phase = -1;                // CYCLE: 1 on / 0 off; else -1
    uint32_t run_s = 0;            // seconds in current relay state
    bool settings_changed = false; // TIMER expired -> mode became Auto; caller saves
    bool publish_now = false;      // a published field other than the *_s counters changed
};
struct State { /* implementer's fields; default-constructed = fresh boot */ };
Outputs step(const Inputs&, Settings&, State&, uint32_t now_ms);   // call every 1 s and after any cmd/want
```

Constants: `LINK_LOST_MS = 60000`, `WANT_FRESH_MS = 90000`.

Rules `step()` implements (spec §2):
- Elapsed time = `now - last_now` per tick, added to saturating accumulators (as `cooler_logic.h` does). No `now - since` comparisons.
- Before the first tick with `mqtt_connected`: relay off, TIMER/CYCLE not counting. On the first connected tick, TIMER/CYCLE (re)start from the beginning (CYCLE in its on phase).
- After first connect: disconnected continuously ≥ 60 s ⇒ `link_lost`, relay off immediately (cuts min_run). Reconnect clears it at once. TIMER/CYCLE keep counting while lost.
- Desired relay by mode: AUTO = fresh `want == 1`; ON = 1; OFF = 0; TIMER = 1 until `timer_h` h elapsed, then mode := Auto (`settings_changed`); CYCLE = alternating `cycle_on` / `cycle_off` minutes starting on.
- min_run: when the relay closes, it stays closed ≥ `min_run` min even if desired goes 0, except mode OFF or `link_lost` (both open at once).

- [ ] **Step 1: Write the failing tests** in `test_dehum.cpp`. Use a tiny local helper `run(Settings&, State&, Inputs, uint32_t& now, uint32_t ms, uint32_t tick=1000) -> Outputs` that steps every `tick` ms and returns the last Outputs. Every test first steps once with `mqtt_connected = true` unless it is testing boot. Tests and their assertions:
  - `"clamp_settings bounds"`: each of min_run 0..60, timer_h 1..24, cycle_off 1..240 at lo−1/lo/hi/hi+1; cycle_on 0 with min_run 0 → 1; cycle_on 241 → 240.
  - `"min_run bumps cycle_on"`: Settings{min_run 40, cycle_on 30} clamp → cycle_on 40; `apply_cmd({min_run=50, cycle_on=20})` → cycle_on 50.
  - `"parse_mode"`: "auto","on","off","timer","cycle" round-trip with `mode_str`; "AUTO", "", "x" → nullopt.
  - `"boot stays off until first connect"`: Settings mode On, step 10 min disconnected → relay 0 throughout, `link_lost` 0; then connected → relay 1.
  - `"timer restarts at first connect"`: mode Timer timer_h 1, 5 min disconnected from boot, then connected: relay 1 for 60 min from connect, `remain_s` at connect == 3600, then relay 0, mode Auto, `settings_changed` true on that tick.
  - `"on / off"`: ON → relay 1; apply OFF while hold active → relay 0 the same tick.
  - `"auto follows fresh want"`: min_run 0; `on_want(1)` → relay 1, `want` 1; `on_want(0)` → relay 0; `on_want(1)` then no want for 89 s → relay 1, at 91 s → relay 0, `want` −1.
  - `"min_run holds through want drop"`: min_run 10; want 1 for 2 min, then want 0 (kept fresh every 30 s) → relay 1 until 10 min after it closed, `hold_s` counts down, then relay 0.
  - `"cycle phases"`: cycle_on 20, cycle_off 40, min_run 10 → on 0–20 min (`phase` 1), off 20–60 (`phase` 0), on again at 60; `remain_s` == 1200 at start.
  - `"resend restarts with new values"`: in Timer (timer_h 2) for 30 min, `apply_cmd({timer_h=3, mode=Timer})` → `remain_s` == 10800; in Cycle 15 min in, `apply_cmd({mode=Cycle, cycle_on=25})` → phase 1, `remain_s` == 1500.
  - `"broker loss cuts after 60 s"`: ON, min_run 10, relay just closed; disconnect: relay 1 at 59 s, relay 0 and `link_lost` 1 at 60 s; reconnect → relay 1, `link_lost` 0.
  - `"timer counts through broker loss"`: Timer 1 h, disconnect at 10 min for 55 min, reconnect → `remain_s` == 300.
  - `"want remembered across modes"`: ON, `on_want(1)`, 30 s later `apply_cmd({mode=Auto})` → relay 1.
  - `"timers survive millis wrap and late ticks"`: start `now = UINT32_MAX − 30000`, Timer 1 h stepping with `tick = 5000` → relay 0 and mode Auto first seen at elapsed ≥ 3600 s and < 3605 s.
  - `"publish_now on changes"`: true on the tick relay or mode changes, false on a following idle tick.

- [ ] **Step 2: Run and verify they fail**

Run: `cd dehumidifier && cmake -S tests -B build && cmake --build build -j`
Expected: compile errors (`dehum_logic.h` missing).

- [ ] **Step 3: Implement `dehumidifier/dehum_logic.h`** with the interfaces above. Header comment mirrors `cooler_logic.h` (pure C++17, spec path). Only `<cstdint>`, `<cstring>`, `<optional>`.

- [ ] **Step 4: Run tests**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all pass, no warnings (`-Werror`).

- [ ] **Step 5: Commit**

```bash
git add dehumidifier/dehum_logic.h dehumidifier/tests .gitignore
git commit -m "feat(dehum): control logic and host tests"
```

---

### Task 2: Dehumidifier ESPHome firmware and rules doc

**Files:**
- Create: `dehumidifier/dehum_esphome.h` — glue like `controller/cooler_esphome.h`: function-local statics for `Settings`, `State`, `Outputs`; one preference `PREF_DEHUM_SETTINGS = 0xDE40015Eu`; `load()` (load, clamp, save back), `save_settings()`.
- Create: `dehumidifier/dehum-v1.yaml`
- Create: `dehumidifier/secrets.yaml.example` (wifi_ssid, wifi_password, wifi_ssid_dev, wifi_password_dev, ap_password, ota_password, mqtt_broker, `dehum_mqtt_username`, `dehum_mqtt_password`)
- Create: `RULES-dehum.md` (repo root; style of `RULES-v4.md`: hardware, modes, settings, MQTT topics + example payloads, logins, install checklist = spec §6 bench list)
- Modify: `README.md` — one line pointing to `dehumidifier/` and `RULES-dehum.md`

**Interfaces:** Consumes Task 1 (`dehum::step`, `apply_cmd`, `on_want`, `parse_mode`, `mode_str`). Produces the MQTT contract of spec §4.1–4.3 that Task 3 and the future LCD use.

YAML decisions:
- `device_name: esp32-dehum`; `esp32: board: esp32dev, framework: type: esp-idf`; WiFi/AP/OTA/captive portal/sntp/logger as the Cooler; CA block copied verbatim from `cooler-v4.yaml`; `client_id: ${device_name}`; `keepalive: 15s`.
- `on_boot` priority −100: `switch.turn_off: relay`, `switch.turn_off: relay_spare`, `dehum_glue::load()`.
- `switch`: `relay` GPIO16 and `relay_spare` GPIO23, both `inverted: true` (low-level-trigger module; confirm on bench in Task 4), `restore_mode: ALWAYS_OFF`, `internal: true`.
- `on_json_message` `dehum/cmd` (qos 0): build a `dehum::Cmd` from integer keys only (`is<int>()`), `mode` via `parse_mode(x["mode"].as<const char*>())` when it is a string; `apply_cmd`; save if it returns true; mark dirty; run control.
- `on_json_message` `dehum/want` (qos 0): if `want` is int 0/1 → `on_want`; run control. (QoS 0 subscriptions are not queued by Mosquitto, so no stale want arrives after reconnect.)
- Control script (1 s interval + after messages): `step`; drive `relay` on change; save when `settings_changed`; dirty on `publish_now`.
- Publish `dehum/data` retained when dirty and every 30 s, fields exactly as spec §4.2 (`want` and `phase` as `null` when −1; `phase` as `"on"`/`"off"`), `uptime_s` from an uptime sensor as the Cooler does.

- [ ] **Step 1: Write the glue header, YAML, secrets example**; copy `secrets.yaml.example` to `dehumidifier/secrets.yaml` filling WiFi/broker/OTA from `controller/secrets.yaml` and leaving `dehum_mqtt_username: dehum`, `dehum_mqtt_password: ""` (the password is created in Task 4). Confirm `git check-ignore dehumidifier/secrets.yaml` prints the path.

- [ ] **Step 2: Compile**

Run: `cd dehumidifier && esphome compile dehum-v1.yaml 2>&1 | tee compile.log | tail -3`
Expected: `INFO Successfully compiled program.`

- [ ] **Step 3: Write `RULES-dehum.md`** and the README line.

- [ ] **Step 4: Commit**

```bash
git add dehumidifier/dehum_esphome.h dehumidifier/dehum-v1.yaml dehumidifier/secrets.yaml.example RULES-dehum.md README.md
git commit -m "feat(dehum): ESPHome firmware for the v1 board"
```

---

### Task 3: Cooler sends the AUTO want

**Files:**
- Modify: `controller/cooler_logic.h` — add after `clamp_settings`:
  ```cpp
  constexpr int DEHUM_RH_BAND = 3;
  struct DehumCfg { int dehum_rh = 65; };          // persisted on its own (not in Settings)
  inline int clamp_dehum_rh(int v);                 // 30 .. 95
  struct DehumAuto { bool humid = false; };
  // want = !ac_relay && humid. humid latches on at rh >= dehum_rh, off at rh < dehum_rh - 3;
  // rh_valid false forces humid false.
  inline bool dehum_want(DehumAuto&, bool ac_relay, bool rh_valid, float rh, int dehum_rh);
  ```
- Modify: `controller/cooler_esphome.h` — `dehumcfg()`, `dehumauto()` statics; `PREF_DEHUM = 0xC001D0E1u` preference for `DehumCfg`; load/clamp/save in `load()`; `save_dehumcfg()`. **`PREF_SETTINGS` and `Settings` stay unchanged.**
- Modify: `controller/cooler-v4.yaml` — `/cmd` handler reads `dehum_rh` (int) → clamp → save; control script computes want with `o.relay`, `o.box_valid && !isnan(id(sht30h).state)`, `id(sht30h).state`; global `g_dehum_want` (int, −1 initial) and on change or every 30 s (existing 30 s interval) while connected `mqtt.publish` topic `dehum/want`, payload `{"want":0|1}`, `retain: false`, `qos: 0`; `cooler/data` gains `dehum_rh` and `dehum_want`.
- Modify: `controller/secrets.yaml.example` — comment noting the controller login also needs write on `dehum/want`.
- Modify: `RULES-v4.md` — `dehum_rh` row in Settings, `dehum_want`/`dehum_rh` in MQTT, the `dehum/want` publish.
- Test: `controller/tests/test_dehum_want.cpp`

**Interfaces:** Produces `dehum/want` messages consumed by Task 2's firmware.

- [ ] **Step 1: Write failing tests** in `test_dehum_want.cpp`:
  - `"dehum_want truth table"`: dehum_rh 65, rh 70 valid: relay 0 → true, relay 1 → false; rh invalid relay 0 → false.
  - `"rh hysteresis"`: rising 64.9 → false, 65.0 → true; falling 62.1 → true, 61.9 → false; rising again 64 → false.
  - `"invalid rh resets latch"`: humid at 70, then invalid → false, then 63 valid → false.
  - `"clamp_dehum_rh"`: 29→30, 30, 95, 96→95.
- [ ] **Step 2: Run, expect compile failure**

Run: `cd controller && cmake -S tests -B build && cmake --build build -j`
Expected: errors for `dehum_want` / `DehumAuto` undefined.

- [ ] **Step 3: Implement** the `cooler_logic.h` additions, then the glue and YAML changes.

- [ ] **Step 4: Run all Cooler tests**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all pass, including every pre-existing test.

- [ ] **Step 5: Compile the Cooler firmware**

Run: `esphome compile cooler-v4.yaml 2>&1 | tee compile.log | tail -3`
Expected: `INFO Successfully compiled program.`

- [ ] **Step 6: Update `RULES-v4.md` and commit**

```bash
git add controller/cooler_logic.h controller/cooler_esphome.h controller/cooler-v4.yaml controller/secrets.yaml.example controller/tests/test_dehum_want.cpp RULES-v4.md
git commit -m "feat(cooler): send dehumidifier AUTO want from AC rest and RH"
```

---

### Task 4: Broker, flashing and bench (user-gated)

Every step here touches live systems: **stop and get the user's go-ahead before each of 1, 3 and 5.**

- [ ] **Step 1: Broker ACL and login** on `ssh champion` (spec §4.5): back up `/etc/mosquitto/acl` and `/etc/mosquitto/passwd` as `*.bak-<UTC timestamp>`; `mosquitto_passwd` a new `dehum` login (random password into `dehumidifier/secrets.yaml`); ACL: `user dehum` readwrite `dehum/#`; `cooler` add `topic write dehum/want`; `coolerpanel` and `coolerapp` add `topic read dehum/data`, `topic read dehum/availability`, `topic write dehum/cmd`. Edit only the cooler blocks. `sudo systemctl reload mosquitto`.
- [ ] **Step 2: Verify ACL** with `mosquitto_sub`/`mosquitto_pub` as each login: `dehum` receives on `dehum/cmd` and `dehum/want`; `coolerpanel` cannot publish to `dehum/want` (message never reaches a `dehum` subscriber).
- [ ] **Step 3: Flash the dehumidifier over USB** (`esphome run dehum-v1.yaml`), relay driving a lamp or meter only. Bench-check spec §1 items: relay polarity (`inverted`), relay stays open through power-up, reset and OTA; module is WROOM (not WROVER — if WROVER, stop and tell the user GPIO16 is unusable).
- [ ] **Step 4: Bench the modes** with `mosquitto_pub` to `dehum/cmd` and `dehum/want`, watching `dehum/data`: every spec §6 bench item 1–4, plus power-up in ON with the broker unreachable → relay stays open.
- [ ] **Step 5: OTA the live Cooler** (`esphome upload cooler-v4.yaml --device <ota host>`), then confirm `cooler/data` still shows the user's tuned `minofftime`, `minruntime`, `fin_cutoff`, `coolerset` (record them before flashing), and shows `dehum_rh` 65 and `dehum_want`.
- [ ] **Step 6: End-to-end**: the dehumidifier in AUTO follows AC rest/cool and `dehum_rh` (spec §6 bench item 5). Report results to the user.
