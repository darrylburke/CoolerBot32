# Single-Relay Cooler Controller (v4) — Design

**Date:** 2026-09-26
**Status:** Draft, awaiting review
**Replaces:** the v3 dual-AC controller (`configc32-dual-v3.yaml`, `RULES.md`), which is retired. `RULES.md` stays as the record of v3.
**Touches:** a new controller (ESP32-WROOM-32U, ESPHome) and the existing CoolerPanel (`CoolerPanel/`).

## 0. Summary

One Frigidaire window AC cools the walk-in. The controller does not switch compressor
power. It fools the AC's own control board, CoolBot-style: a single relay switches a
resistor network on the AC's room-thermistor connector (CN3) so the AC sees either a
"warm" room (13.6 kΩ as built, calls for cooling) or a "cold" room (35 kΩ, stops). The
Frigidaire keeps its own compressor protection.

The AC's original thermistor is moved into the evaporator fins and wired to the ESP32
as a **fin sensor**. It is used for anti-icing and to infer whether the compressor is
running. An SHT30 remains the box sensor. The fin sensor is self-calibrated against the SHT30.

Physical controls shrink to one latching override switch and one status LED. Override,
whether from the switch or from lost connectivity, runs a fixed local thermostat
(on at 5 °C, off at 3 °C). The CoolerPanel (Waveshare ESP32-S3 Smart 86) is the primary
UI. Controller and panel communicate only through the MQTT broker, under the existing
`cooler/` prefix on the broker (TLS), with a reshaped `/data` payload (schema `v: 2`).

### Goals / success criteria
- The box holds `coolerset ± range` using the Frigidaire's own compressor logic.
- The evaporator coil never stays iced: fin lockout applies in every mode, including override.
- Fail-safe: boot, reset, hang, float or controller fault all leave the relay open
  (= "cold" = no cooling request).
- Losing WiFi/broker, or flipping the override switch, drops to a known-good fixed
  thermostat that no remote setting can affect.
- Cooling continues, degraded, if either sensor fails. It continues on a timer if both fail.
- A relay request the AC does not act on (no compressor start) is detected and alarmed.
- Everything the old OLED, buttons and pot did is visible, and settable where appropriate, from the panel.

### Non-goals
- Two AC units, alternation, lead/swap logic (v3 only).
- Local display on the controller.
- Switching compressor or mains power.
- Direct compressor current sensing (a clamp sensor is noted as a future option only).
- Home Assistant auto-discovery changes (HA may read the same topics; not designed for here).

---

## 1. Hardware

### 1.1 Main board
ESP32-WROOM-32U (classic ESP32, external antenna) on a 38-pin USB-C DevKit
(ESPHome board `esp32dev`), powered from a 5 V supply that also feeds the relay coil.
Header pins 5, 17, 18, 23, 25, 26 and 27 are not available on the carrier board and
must never be assigned.

| Function | GPIO | Notes |
|---|---|---|
| SHT30 SDA / SCL | 33 / 22 | I2C, address 0x44, **10 kHz** (100 kHz failed on the sensor cable) |
| Fin thermistor | 35 (ADC1_CH7) | ADC1 only — ADC2 is unusable with WiFi; input-only pin |
| Cooling relay input | 16 | Switches the COM2/NO2 contacts that feed the AC-side block (bench-verified). Low-level trigger; float/high = relay open |
| Other relay input | 19 | Unused: driven HIGH (off), internal |
| Override switch | 21 | **Latching** switch to GND, internal pull-up, 50 ms debounce. Closed (LOW) = override on |
| Status LED | 32 | Via series resistor to GND |

All chosen pins avoid the strapping pins (0, 2, 5, 12, 15), GPIO14 (pulses during boot)
and the flash pins (6–11: D0–D3, CMD, CLK).

**Status LED patterns** (first matching row wins):

| Pattern | Meaning |
|---|---|
| Fast blink (4 Hz) | any sensor fault, or `no_response` |
| Slow blink (1 Hz) | defrost (fin lockout) |
| Solid | relay closed (cooling requested) |
| Off | otherwise (idle / wait / rest) |

### 1.2 Relay module
Amazon B00XAGT2OG: 2-channel 5 V relay module, optocoupled, low-level trigger, 10 A
contacts. Only one channel is used: the relay whose COM2/NO2 contacts feed the AC-side
terminal block, driven by GPIO16.

- **Remove the JD-VCC jumper.** VCC → ESP32 3.3 V, cooling input → GPIO16, other input → GPIO19 (held off), JD-VCC → 5 V,
  relay-side GND → 5 V supply ground. This makes the optocoupler turn fully off at a 3.3 V
  "high" and keeps the relay side isolated from the ESP32.
- GPIO16 is driven as an inverted output: logical ON = pin LOW = relay closed.
- **Known limitation:** the 10 A silver-alloy contacts are not rated for dry-circuit
  (µA) switching and may become intermittent over time. Acceptable for this build. The
  `no_response` detection (§2.9) will surface a failing contact. The upgrade path is a
  gold-contact signal relay or reed relay.

### 1.3 CN3 resistor network (AC side, isolated)

```
CN3 pin 1 ──┬──────[ R_COLD 35 kΩ ]──────┬── CN3 pin 2
            └──[ R_ADD 22 kΩ ]──COM/NO───┘
```

| Relay | Resistance at CN3 | AC sees |
|---|---|---|
| Open (de-energized) | 35 kΩ | cold room (≈ −1 °C) → stops cooling |
| Closed | 35 ‖ 22 ≈ 13.6 kΩ | warm room (≈18 °C) → requests cooling |

- `R_ADD = R_COLD·R_WARM / (R_COLD − R_WARM)`. Values come from the user's measurements of
  the original thermistor.
- R_COLD is always connected, so CN3 is never open-circuit during switching.
- No conductor on the CN3 side may connect to ESP32 GND, 3.3 V, USB, or anything else.

### 1.4 Fin thermistor
The AC's original NTC thermistor, disconnected from CN3, clipped into the evaporator fins
where ice forms first.

```
3.3 V ── R_FIXED 32 kΩ (22 k + 10 k) ── GPIO35 ── thermistor ── GND
```

- ESPHome `adc` (12 dB attenuation, calibrated millivolts) → `resistance` sensor
  (`configuration: DOWNSTREAM`, reference 3.3 V, 32 kΩ), sampled every 5 s.
- Resistance → temperature is done in a template sensor using the Beta equation with
  persisted `fin_beta` / `fin_r0` (§2.7). Defaults are Beta 3950, R0 10 kΩ at 25 °C.
  The ESPHome `ntc` platform is not used because it bakes calibration in at compile time.
- **Fin fault:** measured resistance below 200 Ω (short) or above 1 MΩ (open), or ADC
  above 3.2 V / below 0.05 V, held for 10 s. Clears automatically after 10 s of valid readings.

### 1.5 Frigidaire setup
- Mode: Cool. **Fan: continuous** (not energy-saver cycling). Fin-proxy mode, defrost and
  compressor inference all depend on the fan moving box air over the coil while the
  compressor is off.
- AC setpoint: between the two simulated temperatures. As built (35 kΩ / 13.6 kΩ, i.e.
  ≈ −1 °C / ≈18 °C for a 10 kΩ-at-25 °C NTC) that means the AC's **minimum setpoint,
  ≈16 °C**. If that minimum is above ≈17 °C, use R_ADD = 14 kΩ (closed 10.0 kΩ ≈ 25 °C)
  and a ≈22 °C setpoint instead.
- The AC's own ~3-minute restart delay and its (model-specific) minimum on-time remain in
  force. The relay is a request, not a command. See §6.4 for measuring both.

---

## 2. Control logic

Evaluated once per second in two stages: **demand** (should we cool?) then
**protections** (may we?). The relay closes only if demand is true and no protection holds it open.

### 2.1 Faults and link state
| Condition | Set when | Cleared when |
|---|---|---|
| `sht_fault` | no valid SHT30 reading for 300 s | next valid reading |
| `fin_fault` | out-of-range fin reading for 10 s (§1.4) | 10 s of valid readings |
| `link_lost` | MQTT client disconnected for 60 s continuously (WiFi or broker down) | MQTT connected for 60 s continuously |

While the SHT30 is invalid but its fault is not yet declared, the last demand is held.
The 60 s debounce on `link_lost`, in both directions, stops a broker blip from flapping modes.

### 2.2 Modes (exactly one in force, highest priority first)

Override is in force when **the override switch is closed OR `link_lost`**.

| Mode | When | Demand |
|---|---|---|
| `override` | override in force, SHT30 healthy | fixed thermostat on box temp: **on at ≥ 5.0 °C, off at ≤ 3.0 °C**, held between |
| `override` + fin-proxy | override in force, `sht_fault`, fin healthy | fin-proxy cycle (§2.3) using the fixed 5.0 / 3.0 °C thresholds |
| `normal` | override not in force, SHT30 healthy | thermostat: on when `temp > coolerset + range`, off when `temp < coolerset − range`, held between |
| `fin-proxy` | override not in force, `sht_fault`, fin healthy | fin-proxy cycle (§2.3) using `coolerset ± range` |
| `blind` | `sht_fault` and `fin_fault` (with or without override) | always true, limited by the timed-duty backstop |

- Published `mode` is one of `override`, `override-proxy`, `normal`, `fin-proxy`, `blind`.
  `override_src` is `switch`, `link`, `both`, or `none`.
- The override thresholds are **compile-time constants**
  (`OVR_ON_C = 5.0`, `OVR_OFF_C = 3.0`, in `substitutions`). They are not settable over
  MQTT, so a bad remote write can never affect the fallback.
- Override is **not a setting and not persisted**. It follows the switch position and the
  live link state, including at boot. There is no MQTT command to set it.
- `link_lost` can by definition only be published after the link returns. The panel
  learns about link outages through its own availability/silence alarms. On reconnect the
  controller stays in override for the 60 s debounce, then returns to `normal`.
- On leaving override, the thermostat's held state carries over. It is not reset.
- Mode is re-resolved every tick, so a change takes effect immediately.

### 2.3 Fin-proxy cycle
With the fan running and the compressor off, the fin temperature converges on box air
temperature. `ON_C` / `OFF_C` below are either `coolerset ± range` (`fin-proxy`) or 5.0 / 3.0
(`override-proxy`).

1. **REST** — relay open. Once the relay has been open for at least `settle` minutes, the
   fin temperature is used as the box estimate. If it is at or above `ON_C` → COOL.
   Otherwise stay in REST and keep re-evaluating each tick.
2. **COOL** — demand true until fin temp ≤ `fin_cutoff` (which also triggers defrost)
   or run time reaches `maxrun` minutes → REST.

On entering a proxy mode the cycle starts in REST, and the time already spent with the
relay open counts toward `settle`. (`OFF_C` is unused in the proxy cycle: while the
compressor runs the fin reads coil temperature, not box temperature, so the run ends on
`fin_cutoff` or `maxrun` instead.)

### 2.4 Protections (any one holds the relay open)
Applied in this order:

1. **Fin lockout (defrost)** — if the fin sensor is healthy and fin temp ≤ `fin_cutoff`:
   relay open, `defrost = 1`. Defrost ends when **either**:
   - fin temp ≥ `fin_recover`, **or**
   - the SHT30 is healthy and |fin temp − box temp| ≤ 1.0 °C continuously for 120 s.
     (The coil has reached air temperature, so the ice is gone. This prevents defrost
     from latching forever when the box itself sits below `fin_recover`.)

   Applies in **every mode including override**, and overrides `minruntime`. Box
   temperature does not matter: an iced coil cools poorly and blocks airflow, so icing
   always wins.
2. **Timed-duty backstop** — active **only while `fin_fault`** (the coil has no icing
   protection). Time is divided into windows of `maxrun` minutes. Cooling may run only in
   the first `dutypercent`% of each window. If `dutypercent × maxrun < minruntime`, the run
   lasts `minruntime` and the rest of the window is forfeited. Windows free-run from the
   moment the backstop activates.
3. **`minruntime`** — once closed, the relay stays closed at least this long. Only
   defrost can break it.
4. **`minofftime`** — once open, the relay stays open at least this long. Enforced from
   power-up (the relay is treated as having opened at boot).

### 2.5 Pull-down: no duty cycle
A pull-down from ambient (e.g. 20 °C → 4 °C) runs continuously. There is deliberately no
duty cycle in `normal` or `override` mode:

- A window AC is designed for continuous running, so long runs are not a wear problem.
  The pull-down risk is icing, which fin lockout already covers.
- While the box is warm, the coil stays above 0 °C and the AC runs flat out: the fastest
  possible pull-down.
- As the box cools below roughly 8–10 °C the coil starts to reach `fin_cutoff`, and fin
  lockout produces cool → defrost → cool cycles by itself. The defrost share grows as the
  box gets colder, which is the expected slow approach to setpoint seen in the v3 history.
- The timed-duty backstop (§2.4) exists only for a failed fin sensor.

### 2.6 Run state (published as `state`)
| `state` | Meaning |
|---|---|
| `cooling` | relay closed |
| `defrost` | fin lockout holding the relay open |
| `wait` | demand true but `minofftime` holding |
| `rest` | demand true but the duty backstop is holding, or fin-proxy REST before `settle` has elapsed (box temp not yet known) |
| `idle` | no demand |

`hold_s` is the seconds remaining on whichever timer is currently determining the relay
(`minruntime`, `minofftime`, duty rest, or fin-proxy `settle`), or 0.

### 2.7 Fin sensor calibration
Goal: fit Beta and R0 for the reused thermistor, using the SHT30 as the reference.

- Started by `{"calibrate":1}` (or the panel button). Aborted by `{"calibrate":0}`.
  A reboot aborts a run in progress; collected points are discarded.
- **A point** (fin ohms, SHT30 °C) is recorded when all of these hold:
  - the relay has been open at least `settle` minutes and `compressor = 0`;
  - SHT30 and fin sensor are both healthy;
  - over the last 120 s, SHT30 temp varied ≤ 0.2 °C and fin resistance varied ≤ 1 %;
  - the SHT30 temp is at least 1.0 °C away from every point already held.
- Up to 12 points are held. Once there are **≥ 4 points spanning ≥ 8 °C**, fit
  `1/T = 1/T0 + (1/B)·ln(R/R0)` by least squares on (ln R, 1/T) with T0 = 298.15 K, giving
  B and R0. `fin_cal_err` is the RMS temperature residual in °C.
- **Accept** if 2500 ≤ B ≤ 5500 and `fin_cal_err` ≤ 1.0 °C. Then persist `fin_beta`,
  `fin_r0`, set `fin_cal = 1`, and end the run. **Reject** otherwise: keep the previous
  values, end the run, and publish `fin_cal_err` so the panel can show why.
- `{"fincal_reset":1}` restores the defaults and sets `fin_cal = 0`.
- Because points need the relay open for `settle`, a pull-down yields none (its relay-open
  gaps are short defrosts). Procedure: with the box empty, after the first pull-down, pause
  cooling (`coolerset` 40) and calibrate while the box warms back toward ambient, sweeping
  ≈4 → 20 °C. The curve is extrapolated below that range to the ≈0 °C cutoff; for an NTC this
  is typically within 0.5 °C.
- A run that has not finished after 48 h ends without changing `fin_beta`/`fin_r0`.
- Fin lockout and compressor inference work while uncalibrated (defaults), just less accurately.

### 2.8 Compressor inference (fin slope)
There is no direct compressor sensing. `compressor` (0/1) is inferred from the fin temperature:

- `fin_slope` = least-squares slope of the fin temperature over the last 60 s (12 samples
  at 5 s), in °C/min.
- **Latches to 1** when `fin_slope ≤ −0.5 °C/min` (coil being chilled).
- **Latches to 0** when **either**:
  - `fin_slope ≥ +0.3 °C/min` (coil warming), or
  - the relay has been open ≥ 60 s and the SHT30 is healthy and |fin − box| ≤ 1.0 °C.
- **Otherwise holds** its previous value. Steady running (a cold, flat coil) keeps
  `compressor = 1`, and a steady rest keeps it 0.
- It is 0 at boot. It is not evaluated (published `null`) while `fin_fault`.

Thresholds are compile-time constants, tuned during the characterisation test (§6.4).
`compressor` is diagnostic only. It never drives the relay, except through `no_response` below.

### 2.9 No-response detection
`no_response = 1` when the relay has been closed continuously for **10 min**, `compressor`
has stayed 0 for that whole period, and the fin sensor is healthy. Likely causes: bad relay
contact, CN3 wiring, AC switched off or unplugged, or the AC in the wrong mode.

It clears as soon as `compressor` becomes 1, or when the relay opens. It is alarm-only:
the controller keeps running its normal logic. It is not evaluated while `fin_fault`.

### 2.10 Startup and persistence
- Relay open at boot. Nothing energises the relay from a boot event.
- `minofftime` counts from boot.
- Override follows the switch position read at boot. `link_lost` starts false, and is set
  if MQTT does not connect within 60 s of boot.
- All settings, `fin_beta`/`fin_r0`/`fin_cal`, and the history arrays are persisted
  (`restore_value`) and restored on boot.
- The ESP32 task watchdog resets a hung firmware, and reset releases GPIO16 → relay open.

### 2.11 Settings
Persisted. Settable over `/cmd`. Clamped on every write. They do not affect override's fixed thermostat.

| Key | Unit | Bounds | Default | Notes |
|---|---|---|---|---|
| `coolerset` | °C | 2 .. 40 | 4 | |
| `range` | °C | 0 .. 5 | 2 | hysteresis either side |
| `fin_cutoff` | °C | −5 .. 5 | 1 | defrost entry; 1 not 0 because a thawing ice pack held the bead at 0.2–0.4 °C in the 2026-09-28 bench test |
| `fin_recover` | °C | `fin_cutoff`+1 .. 10 | 3 | defrost exit; a write to `fin_cutoff` that would violate this bumps `fin_recover` to `fin_cutoff + 1` |
| `settle` | min | 2 .. 30 | 10 | fin-proxy and calibration settle time |
| `minofftime` | min | 0 .. 30 | 5 | set ≥ the AC's measured restart delay (§6.4) |
| `minruntime` | s | 0 .. 600 | 180 | 0 disables; set ≥ the AC's measured minimum on-time (§6.4) |
| `maxrun` | min | 1 .. 60 | 10 | fin-proxy max run; duty window length |
| `dutypercent` | % | 1 .. 100 | 50 | backstop duty |
| `sampleinterval` | s | 10 .. 3600 | 3600 | history spacing |

Removed from v3: `screentimeout`, and the `maxrun > minofftime` constraint.

---

## 3. MQTT contract

Prefix `cooler/` on the broker (`mqtt_broker`), port 8883, TLS verified against the private CA
(ESP-IDF framework: ESPHome supports a custom CA only there). Logins and ACLs: controller
login (`mqtt_username`, read/write `cooler/#`); panel login (`panel_mqtt_username`; read `cooler/data` and
`cooler/availability`, write `cooler/cmd`). No Home Assistant discovery.

| Topic | Direction | Retained | Content |
|---|---|---|---|
| `/data` | controller → | yes | full state (§3.1) |
| `/availability` | controller → | yes | `online` on connect; `offline` via LWT |
| `/cmd` | → controller | no | JSON commands (§3.2) |

### 3.1 `/data`
Published every 30 s, and immediately on any change of `relay`, `state`, `mode`,
`override_src`, `sht_fault`, `fin_fault`, `defrost`, `compressor`, `no_response`, a
setting, or calibration progress.

```json
{
  "v": 2,
  "temp": 4.8, "humidity": 78.2,
  "fin_temp": 1.6, "fin_ohms": 28410, "fin_slope": -0.8,
  "mode": "normal", "override_src": "none", "state": "cooling",
  "relay": 1, "cool_call": 1, "defrost": 0, "compressor": 1,
  "sht_fault": 0, "fin_fault": 0, "no_response": 0,
  "run_s": 142, "off_s": 0, "hold_s": 38,
  "coolerset": 4, "range": 2, "maxrun": 10, "dutypercent": 50,
  "minofftime": 5, "minruntime": 180, "sampleinterval": 3600,
  "fin_cutoff": 1, "fin_recover": 3, "settle": 10,
  "fin_cal": 1, "fin_beta": 3912, "fin_r0": 10240, "fin_cal_err": 0.3,
  "cal_active": 0, "cal_points": 0, "cal_span": 0.0,
  "hist_n": 20, "hist_interval_s": 3600, "hist_last_ts": 1790000000,
  "temp_hist": [5,5,4], "hum_hist": [78,79,79],
  "uptime_s": 86400
}
```

- `v` — schema version. Consumers must ignore payloads without `"v": 2`.
- `temp`, `humidity`, `fin_temp` are `null` when invalid, never `nan`. `fin_ohms`,
  `fin_slope` and `compressor` are `null` on fin fault.
- `cool_call` — demand from §2.2 before protections. `relay` — actual relay state.
- `run_s` — seconds the relay has been closed (0 while open). `off_s` — seconds it has
  been open (0 while closed). `hold_s` — see §2.6.
- `fin_cal_err` — RMS residual of the last fit attempt in °C, or `null` if never fitted.
- `cal_points` / `cal_span` — progress of the active calibration run.
- History: identical semantics to v3 `RULES.md` (≤ 20 integer samples, index 0 oldest,
  sampled only while the SHT30 is valid, persisted, `{"clearhist":1}` resets).

### 3.2 `/cmd`
JSON object, any subset of these keys:

- **Settings:** every key in §2.11. Each is clamped on receipt.
- **Actions:**
  - `calibrate` (1 start / 0 abort)
  - `fincal_reset` (1)
  - `clearhist` (1)

Unknown keys are ignored. After applying, the logic re-runs immediately and `/data` is
republished. Removed from v3: `screentimeout`, `wake`, and `override` (override is now
physical switch or link loss only).

---

## 4. CoolerPanel changes

Modified in place in `CoolerPanel/` (now a folder of this repository). The platform layer, history ring,
chart, command/reconcile flow, boot, setup portal and night dimming are unchanged.

### 4.1 Model (`shared/model/`)
- **`cooler_state`** — mirrors `/data` v2.
  - Remove: `ac1`/`ac2` and per-unit fields, `lead`, `active_unit`, `window_*`,
    `swap_in_s`, `pot_pct`, `screentimeout`, `ac_on`, `in_duty`, `override`.
  - Add: `fin_temp`, `fin_ohms`, `fin_slope`, `override_src`, `state`, `relay`,
    `defrost`, `compressor`, `fin_fault`, `no_response`, `run_s`/`off_s`/`hold_s`,
    `fin_cutoff`, `fin_recover`, `settle`, `fin_cal`, `fin_beta`, `fin_r0`,
    `fin_cal_err`, `cal_active`, `cal_points`, `cal_span`.
  - Validity flags for nullable values.
  - Parser rejects payloads without `"v": 2`.
- **`bounds`** — the §2.11 table. `fin_recover` has a dynamic minimum of `fin_cutoff + 1`.
  `maxrun` is a plain 1..60. `screentimeout` is removed. The existing panel-side divergences
  are kept (`dutypercent` step 5 floor 5; `sampleinterval` preset chips).
- **`commands`** — add `calibrate`, `fincal_reset`; remove `wake` and `override`.
- **`alarm`** — replace `SensorFault` with `BoxSensorFault` (`sht_fault`) and
  `FinSensorFault` (`fin_fault`), and add `NoResponse` (`no_response`). Each is acknowledged
  separately. The priority order is ControllerOffline, ControllerSilent, NoResponse,
  BoxSensorFault, FinSensorFault, NotKeepingUp. `ControllerOffline`, `ControllerSilent` and
  `NotKeepingUp` (8 °C / 6 h) are unchanged.
  - BoxSensorFault text names the current fallback: "cooling on fin sensor"
    (`fin-proxy` / `override-proxy`) or "cooling on timer" (`blind`).
  - FinSensorFault text: "icing protection on timer backstop".
  - NoResponse text: "cooling requested 10 min, compressor not running — check AC power,
    mode, relay and CN3 wiring".

### 4.2 Screens
- **Trend** — header and chart unchanged. The two AC tiles are replaced by one wide status tile:
  - line 1: `state` as a large word, colour-coded (cooling blue, idle grey, defrost amber,
    wait/rest dim), then time in that state, then fin temp (`--.-°` if invalid) and a small
    compressor glyph (filled = running, hollow = stopped, absent when `null`);
  - line 2: mode, plus the holding protection and its countdown
    (e.g. `normal · min-run 38s`, `fin-proxy · settle 4m`);
  - while in override, a prominent `OVERRIDE 5°/3°` badge with its source (`switch` or
    `link`), because the panel's `coolerset`/`range` are not in effect.

  The footer shows humidity and `set N ±R`.
- **Settings** — groups:
  - primary: `coolerset`, `range`. While override is in force, a banner states that
    override's fixed 5 °C / 3 °C thermostat is active and these settings take effect when
    it ends. There is no override toggle (override is physical/link only);
  - **Coil**: `fin_cutoff`, `fin_recover`, `settle`;
  - **Compressor**: `minofftime`, `minruntime`, with a hint to set them from the measured
    AC timings (§6.4);
  - **Fallback** ("used only when a sensor fails"): `maxrun`, `dutypercent`;
  - Advanced: `sampleinterval`.
- **Detail** — read-only:
  - mode, `override_src`, state, `run_s`/`off_s`/`hold_s`;
  - fin temp, ohms, slope, `compressor`;
  - `fin_cal`, Beta, R0, `fin_cal_err`;
  - uptime, fault flags, `no_response`, broker link.

  **Calibrate** section:
  - Start button;
  - while `cal_active`: progress `N points · span X °C (need ≥4 / ≥8 °C)` and an Abort button;
  - "Reset to defaults" behind a confirm step.

### 4.3 Tests and fixtures
- Replace the v3 fixtures with v2 fixtures: normal cooling, defrost, override (switch),
  fin-proxy, blind, no-response, calibration in progress, and a v3-shaped payload (must be rejected).
- Update `test_state`, `test_bounds`, `test_commands`, `test_alarm`, `test_app_wiring`.
- Update `tests/capture_fixtures.sh` for v2 once the controller is publishing.

---

## 5. Controller implementation structure

```
controller/
  cooler-v4.yaml         ESPHome: board, sensors, relay, switch, LED, MQTT, globals
  cooler_logic.h         all decisions; no ESPHome/Arduino dependencies
  cooler_esphome.h       ESPHome glue: state instances + preferences
  tests/                 doctest + CMake host tests and scenario sims
RULES-v4.md              operator-facing rules for this controller (style of RULES.md)
```

- `cooler_logic.h` exposes pure types and functions:
  - `Inputs` (sensor readings + validity, switch level, MQTT connected flag, commands)
  - `Settings` (§2.11, with `clamp()`)
  - `LogicState` (timers, latches, thermostat hold states, fin-proxy phase, duty window,
    fin sample ring, compressor latch, calibration points)
  - `Outputs` (relay, LED pattern, mode, override_src, state, hold_s, compressor,
    no_response, publish-now flag)
  - `Outputs step(const Inputs&, const Settings&, FinCal&, LogicState&, uint32_t now_ms)` (FinCal is updated by a finished calibration run)
  - `CalFit fit_beta(const CalPoint*, int n)`
- The YAML includes it plus `cooler_esphome.h` via `esphome: includes:`. ESPHome declares
  `globals:` before project headers, so the Settings / FinCal / LogicState / Outputs instances
  live in `cooler_esphome.h` as function-local statics, and Settings and FinCal persist via
  `global_preferences->make_preference<T>()`. A 1 s `interval:` runs the `control` script, which
  fills `Inputs` (including `id(mqtt_client).is_connected()`), calls `step()`, drives GPIO16, and
  flags `/data` for publishing. A 125 ms interval renders the LED pattern. The MQTT
  `on_json_message` handler updates Settings and calibration actions, then re-runs `control`.
  **The YAML contains no control decisions.**
- `millis()` wrap is handled with unsigned subtraction throughout.

---

## 6. Testing and verification

### 6.1 Host unit tests (`controller/tests`, `ctest`)
- Each mode's demand, and mode priority. Normal hysteresis edges and the hold band.
- Override: fixed on at 5.0 / off at 3.0 regardless of `coolerset`/`range`. Entered by the
  switch, by 60 s of MQTT disconnect, and by both. Not entered by a 59 s disconnect. Exits
  only after the switch opens and 60 s of connection. The held thermostat state carries
  across the transition.
- Fin lockout entry at `fin_cutoff`. Exit at `fin_recover`. Exit via the fin≈box rule with
  the box below `fin_recover`. No exit via fin≈box while the SHT30 is faulted. Lockout under
  override. Defrost breaking `minruntime`.
- `minofftime` from boot.
- Backstop active only while `fin_fault`. `minruntime` forfeit case.
- Fault debounce and auto-recovery for both sensors. Last demand held before `sht_fault`.
- Fin-proxy and override-proxy: REST→COOL on settled fin temp at or above the relevant
  on-threshold. COOL→REST on cutoff and on `maxrun`.
- Compressor inference: latches on at the falling-slope threshold, holds through a flat
  cold coil, latches off on the rising slope and on the fin≈box rule, `null` on fin fault.
- `no_response`: sets after 10 min closed with `compressor = 0`, not at 9:59, clears on
  compressor start or relay open, not evaluated on fin fault.
- Every clamp at `lo−1`, `lo`, `hi`, `hi+1`. The `fin_recover` bump.
- Beta fit recovers a known B (±20) and R0 (±2 %) from synthetic points. Rejects B outside
  2500..5500. Point gating rejects unsettled, unstable, compressor-on, and too-close samples.

### 6.2 Scenario simulations (`ctest`)
Second-by-second, against a simple thermal model:
- box temperature drifts toward ambient;
- the AC applies a configurable restart delay and minimum on-time to the relay request;
- a running compressor chills the coil, and the chilled coil cools the box;
- the coil ices below 0 °C;
- with the compressor off, the fan pulls the coil toward box temperature.

| Scenario | Must hold |
|---|---|
| Normal pull-down 20 → 4 °C | converges into band; no relay transition violates `minofftime`/`minruntime`; runs continuously until the fin first reaches cutoff |
| Coil icing, normal mode | relay never closed for more than one tick with fin ≤ `fin_cutoff` |
| Box settles below `fin_recover` during defrost | defrost exits via the fin≈box rule; no permanent lockout |
| Override (switch) 24 h | box held within 3.0–5.0 °C band plus AC lag; defrost still fires; same fin invariant |
| Link lost 30 min then restored | override after 60 s; back to `normal` 60 s after reconnect |
| SHT30 fails mid-run | `fin-proxy` after 300 s; box held within ±3 °C of the band |
| Both sensors fail | measured duty within 1 % of `dutypercent` |
| AC ignores the relay (model "unplugged") | `no_response` at 10 min |
| AC min-on 5 min, `minruntime` 3 min | compressor inference tracks the model's real compressor state within 90 s |
| Boot | relay open ≥ `minofftime`; override follows the switch |

### 6.3 Bench checklist
1. With the AC unplugged, measure the CN3 network: relay open ≈ 35 kΩ, closed ≈ 13.6 kΩ. No
   continuity from either CN3 lead to ESP32 GND or 3.3 V.
2. The relay stays open through power-up, reset, OTA, and a forced crash (meter on COM/NO).
3. A 10 kΩ pot in place of the fin probe sweeps `fin_temp`. Open and shorted inputs raise
   `fin_fault` within ~10 s.
   Unplug the SHT30: `temp` goes `null` within 30 s and `sht_fault` sets within about 330 s.
4. The override switch toggles `mode` to `override` immediately. Pulling the broker (or
   WiFi) enters override after 60 s, and it recovers 60 s after reconnect.

### 6.4 Installation and AC characterisation
5. **AC restart delay and minimum on-time.** With the box warm, the fin calibrated or on
   defaults, and the fan continuous:
   1. Close the relay (temporarily set `minofftime` 0 and `minruntime` 0). Record the time until
      `fin_slope` goes clearly negative. That is the **restart delay**.
   2. About 30 s after the compressor starts, open the relay. Record how long the fin keeps
      falling before it turns upward. That is the **AC minimum on-time**.
   3. Repeat 3 times. Take the maximum of each.
   4. Set `minofftime` ≥ restart delay (rounded up to whole minutes), `minruntime` ≥ AC
      minimum on-time, and raise `fin_cutoff` by the fin overshoot observed after the relay
      opened, so the coil bottoms out near 0 °C rather than below it.
   5. Tune the compressor-inference slope thresholds if the observed slopes differ
      materially from ±0.5/+0.3 °C/min.
6. On the AC: confirm the Frigidaire starts cooling on relay close and stops on open.
   Let the fin lockout trip naturally and confirm the fan keeps running during defrost.
7. Run calibration after the first pull-down, cooling paused (§2.7). Accepted fit with `fin_cal_err` < 0.5 °C.
8. Unplug the AC with the relay closed and confirm `no_response` fires after 10 min.
9. Panel: simulator against the live broker, then on hardware.

---

## 7. Build order

| Phase | Deliverable | Verified by |
|---|---|---|
| A | `cooler_logic.h`, host unit tests, scenario sims | `ctest` green |
| B | `cooler-v4.yaml`: sensors, relay, switch, LED, MQTT v2, link-loss, calibration | §6.3 items 1–4; `/data` v2 visible on the broker |
| C | CoolerPanel model, alarms, screens and fixtures for v2 | panel `ctest`; sim against the live controller |
| D | Install on the Frigidaire, characterisation, calibration, `RULES-v4.md` finalised | §6.4 items 5–9 |

---

## 8. Decisions taken

| Decision | Choice | Rationale |
|---|---|---|
| AC interface | CN3 resistor trick, one relay | Frigidaire keeps its own compressor protection |
| Units | one Frigidaire | v3 alternation not needed |
| Anti-icing | reused AC thermistor as fin sensor, lockout in all modes; icing beats box temp | reacts to real ice; an iced coil can't cool anyway |
| Defrost exit | fin ≥ `fin_recover` OR fin ≈ box for 2 min | cannot latch forever when the box is colder than `fin_recover` |
| Fin backstop | timed duty only while fin sensor faulted | coil is otherwise unprotected |
| Duty cycling (normal/override, incl. pull-down) | none | fin lockout creates the cycling when icing starts; continuous running is fastest |
| Override | fixed 5 °C on / 3 °C off thermostat, not settable remotely | known-good fallback immune to bad remote settings |
| Override trigger | latching switch OR 60 s MQTT disconnect | local control plus automatic network-loss fallback |
| Override over MQTT | removed | override means "don't trust remote"; physical/link only |
| SHT30 failure | fin-proxy (rested fin temp as box estimate), then blind timer | keeps cooling with best available signal |
| Fin calibration | on-device Beta fit against SHT30 | no manual two-point measurement |
| AC timing | measured once via fin slope at install; used to set `minruntime`/`minofftime`/`fin_cutoff` | Frigidaire min-on is model-specific and undocumented |
| Compressor sensing | inferred from fin slope; no current clamp | no extra hardware; enough for diagnostics and `no_response` |
| Firmware | ESPHome, logic in a testable C++ header | continuity with v3, plus real tests |
| MQTT | same prefix, `/data` v2 with a schema version, `/ac1`/`/ac2` removed | one controller; the panel can detect stale v3 payloads |
| Panel | modified in place | v3 retired |
| Relay | B00XAGT2OG ch 1, JD-VCC jumper removed | isolation and a clean 3.3 V drive |

## 9. Out of scope
- Gold-contact relay upgrade (noted in §1.2 as future work).
- Current-clamp compressor sensing (fallback if fin-slope inference proves unreliable).
- Home Assistant discovery / entities.
- Audible alarm, push notifications (unchanged from the panel spec).
