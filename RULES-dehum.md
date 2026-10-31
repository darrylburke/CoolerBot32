# Dehumidifier Controller — Rules

A small plug-in dehumidifier in the walk-in. The old v1 ESP32 board switches its
power with relay 1, and has a spare relay 2. In AUTO both follow four rules on
the board's own temperature / humidity sensor (no link to the Cooler).
The dehumidifier resumes running by itself when power returns, so switching its
power is all the control needed.

Implemented in `dehumidifier/dehum-v1.yaml` + `dehumidifier/dehum_logic.h`.
Full design: `docs/superpowers/specs/2026-10-28-dehumidifier-controller-design.md`;
AUTO rules: `docs/superpowers/specs/2026-10-31-dehum-rules-and-temp-humidity-tab-design.md`.

# Hardware
| Part | Pin | Notes |
|---|---|---|
| Board | — | ESP32 DevKitC V4 (WROOM-32). A WROVER module would use GPIO16 for PSRAM |
| Relay 1 (dehumidifier) | GPIO16 | low-level trigger (low = closed). COM/NO in the **hot** wire only; neutral and ground straight through |
| Relay 2 (spare) | GPIO23 | low-level trigger. Driven only by AUTO rules with relay 2; off in every other mode |
| Humidity sensor | SDA GPIO21 / SCL GPIO22 | HDC1080 at I²C 0x40, 10 kHz (the board's own; not an SHT30) |

Never use GPIO14 (pulses at boot) or GPIO12 (strapping pin: stops boot if pulled
high). Relay open = dehumidifier off, so any ESP32 failure leaves it off.
Fit a 5 A fuse or breaker on the switched line. In North America the hot wire is
black / the narrow plug blade.

# Modes
| Mode | Relay 1 | Relay 2 | Ends |
|---|---|---|---|
| auto | rules | rules | another mode |
| on | on | off | another mode |
| off | off at once (ignores `min_run`) | off at once | another mode |
| timer | on for `timer_min` minutes | off | time up → **auto** |
| cycle | on `cycle_on` min, off `cycle_off` min, repeating, starting on | off | another mode |

- on, timer and cycle ignore the rules and the sensor.
- Sending timer or cycle again restarts it (with any new values in the same message).
- After a reboot the saved mode returns; timer and cycle start from the beginning.

# AUTO rules
Four rules, each "if *sensor* is *above/below* *value*, turn relay *1/2* *on/off*".
For each relay: on when some enabled, met rule turns it on **and** no enabled, met
rule turns it off. **Off wins**; no rule met = off.

A rule is met with hysteresis (band 3 %RH or 1 °C):
- above: met at reading ≥ value, unmet below value − band;
- below: met at reading ≤ value, unmet above value + band.

A disabled rule is never met. No reading for 30 s (or no value) = not met. A
command that changes a rule makes it be judged afresh from its threshold.
Rules are judged in every mode (`rN_match`) but only act in auto.

Defaults: r1 humidity above 65 → relay 1 on (enabled); r2 humidity below 55 →
relay 1 off; r3 temp above 10 → relay 2 on; r4 temp below 2 → relay 2 on
(r2–r4 disabled).

# Protections
All cover both relays.
1. **`min_run`** — once on, each relay stays on at least `min_run` minutes, timed
   on its own (also when a mode change would turn it off). OFF and broker loss cut it short.
2. **Boot** — both off until the broker has connected once.
3. **Broker lost 60 s** — both off in every mode; the mode is kept and control
   resumes as soon as the broker is back. timer and cycle keep counting.
4. **Sensor** — no reading for 30 s means no rule is met, so auto turns both off
   (after any `min_run`); on / timer / cycle carry on.

# Settings (MQTT `dehum/cmd`, persisted, clamped)
| Key | Unit | Bounds | Default |
|---|---|---|---|
| `min_run` | min | 0 .. 60 | 10 |
| `timer_min` | min | 1 .. 1440 (24 h) | 120 |
| `cycle_on` | min | max(1, `min_run`) .. 240 | 30 |
| `cycle_off` | min | 1 .. 240 | 30 |
| `rN_en` (N = 1..4) | 1 enabled / 0 disabled | 0 .. 1 | r1 1, others 0 |
| `rN_sensor` | 0 temperature / 1 humidity | 0 .. 1 | r1 1, r2 1, r3 0, r4 0 |
| `rN_cmp` | 0 below / 1 above | 0 .. 1 | r1 1, r2 0, r3 1, r4 0 |
| `rN_value` | whole °C or %RH | temp −20 .. 50, humidity 0 .. 100 | r1 65, r2 55, r3 10, r4 2 |
| `rN_relay` | 1 dehumidifier / 2 spare | 1 .. 2 | r1 1, r2 1, r3 2, r4 2 |
| `rN_action` | 1 on / 0 off | 0 .. 1 | r1 1, r2 0, r3 1, r4 1 |

Raising `min_run` above `cycle_on` raises `cycle_on` with it. `rN_value` is
clamped to its rule's sensor; changing `rN_sensor` re-clamps it. Non-integer values
and unknown mode names are ignored. (`dehum_rh` is gone: r1 replaced it. The
firmware with rules starts once from these defaults, mode auto.)

# MQTT
Same broker as the Cooler, port 8883, TLS against the same private CA.

| Topic | Direction | Retained | Payload |
|---|---|---|---|
| `dehum/cmd` | panel/app → dehumidifier | no | `{"mode":"timer","timer_min":90}`, `{"mode":"cycle","cycle_on":20,"cycle_off":40}`, `{"min_run":15}`, `{"r2_en":1,"r2_value":50}` |
| `dehum/data` | dehumidifier → all | yes | every 30 s and on change, below |
| `dehum/availability` | dehumidifier | yes | `online` / `offline` (LWT) |
| `dehum/history` | Node-RED → panel/app | no | 24 h in 1-min slots: `{"v":1,"t0":…,"interval_s":60,"hum":[…],"temp":[…],"relay":[…]}` (`nodered/dehum-history.json`) |

`dehum/data`: `v` (1), `mode`, `relay`, `relay2`, `link_lost`, `hold_s` and
`hold2_s` (min run left, relay 1 / 2), `remain_s` (timer left, or cycle phase
left), `phase` (`on`/`off`, cycle only, else null), `temp` and `humidity`
(local sensor, null if no reading in 30 s), `min_run`, `timer_min`, `cycle_on`,
`cycle_off`, `rN_en`, `rN_sensor`, `rN_cmp`, `rN_value`, `rN_relay`, `rN_action`,
`rN_match` (0/1, rule met) for N = 1..4, `run_s` (relay 1), `uptime_s`.

| Login | Used by | Broker ACL |
|---|---|---|
| `dehum` (`dehum_mqtt_username`) | the dehumidifier | read/write `dehum/#` |
| controller login | the Cooler | + write `dehum/want` (no longer used: nothing subscribes; drop with the next Cooler flash) |
| panel and app logins | CoolerPanel, CoolerApp | + read `dehum/data`, `dehum/availability`, `dehum/history`; write `dehum/cmd` |
| controller login (also Node-RED's) | Cooler, Node-RED | + read `dehum/data`, `dehum/availability`; write `dehum/history` |

The Android app's **Temp/Humidity** tab shows and sets all of this (`CoolerApp/README.md`).

Passwords live only in the git-ignored `dehumidifier/secrets.yaml`
(keys in `secrets.yaml.example`).

# Build
Host tests: `cd dehumidifier && cmake -S tests -B build && cmake --build build -j && ctest --test-dir build`.
Firmware (ESPHome 2025.2): `esphome run dehumidifier/dehum-v1.yaml`.

# Install checklist
Bench first, with the relay switching a lamp or meter, not mains:
1. Relay polarity: `on` closes COM/NO. Relay stays open through power-up, reset, OTA and a crash.
2. Each mode via `mosquitto_pub` to `dehum/cmd` behaves as in Modes; watch `dehum/data`.
3. Relay 2 polarity, same as relay 1: enable a rule with relay 2 (e.g. `{"r3_en":1,"r3_value":-20}`) in auto, check COM/NO closes; set mode `on` → relay 2 opens after `min_run`; `off` → both open at once.
4. Block the broker / WiFi in `on`: relay opens after 60 s, closes again on reconnect (in auto, both relays).
5. Power up in `on` with the broker unreachable: relay stays open.
6. In auto with the default rules: relay 1 closes at RH ≥ 65 and opens below 62 (after `min_run`); unplug the sensor: it opens after 30 s.
Then wire the hot line through COM/NO with the fuse, in a closed box.
