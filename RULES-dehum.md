# Dehumidifier Controller — Rules

A small plug-in dehumidifier in the walk-in. The old v1 ESP32 board switches its
power with a relay. In AUTO it runs while the Cooler controller (v4) reports
the AC resting and its own humidity sensor reads humid.
The dehumidifier resumes running by itself when power returns, so switching its
power is all the control needed.

Implemented in `dehumidifier/dehum-v1.yaml` + `dehumidifier/dehum_logic.h`.
Full design: `docs/superpowers/specs/2026-10-28-dehumidifier-controller-design.md`.

# Hardware
| Part | Pin | Notes |
|---|---|---|
| Board | — | ESP32 DevKitC V4 (WROOM-32). A WROVER module would use GPIO16 for PSRAM |
| Dehumidifier relay | GPIO16 | low-level trigger (low = closed). COM/NO in the **hot** wire only; neutral and ground straight through |
| Spare relay | GPIO23 | unused, held off by the firmware |
| Humidity sensor | SDA GPIO21 / SCL GPIO22 | HDC1080 at I²C 0x40, 10 kHz (the board's own; not an SHT30) |

Never use GPIO14 (pulses at boot) or GPIO12 (strapping pin: stops boot if pulled
high). Relay open = dehumidifier off, so any ESP32 failure leaves it off.
Fit a 5 A fuse or breaker on the switched line. In North America the hot wire is
black / the narrow plug blade.

# Modes
| Mode | Relay | Ends |
|---|---|---|
| auto | on while the Cooler sends `want: 1` (AC resting) **and** local RH is humid | another mode |
| on | on | another mode |
| off | off at once (ignores `min_run`) | another mode |
| timer | on for `timer_h` hours | time up → **auto** |
| cycle | on `cycle_on` min, off `cycle_off` min, repeating, starting on | another mode |

- on, timer and cycle ignore the AC and the humidity.
- Sending timer or cycle again restarts it (with any new values in the same message).
- After a reboot the saved mode returns; timer and cycle start from the beginning.

# Protections
1. **`min_run`** — once on, the relay stays on at least `min_run` minutes, even if
   the AC starts cooling (the AC never waits). OFF and broker loss cut it short.
2. **Boot** — relay off until the broker has connected once.
3. **Broker lost 60 s** — relay off in every mode; the mode is kept and control
   resumes as soon as the broker is back. timer and cycle keep counting.
4. **Cooler silent 90 s** — auto treats it as `want: 0`; on / timer / cycle carry on.
5. **Humidity** — humid turns on at RH ≥ `dehum_rh` and off below `dehum_rh − 3`.
   No reading for 30 s means not humid, so auto stays off; on / timer / cycle carry on.

# Settings (MQTT `dehum/cmd`, persisted, clamped)
| Key | Unit | Bounds | Default |
|---|---|---|---|
| `min_run` | min | 0 .. 60 | 10 |
| `timer_h` | h | 1 .. 24 | 2 |
| `cycle_on` | min | max(1, `min_run`) .. 240 | 30 |
| `cycle_off` | min | 1 .. 240 | 30 |
| `dehum_rh` | %RH | 30 .. 95 | 65 |

Raising `min_run` above `cycle_on` raises `cycle_on` with it. Non-integer values
and unknown mode names are ignored.

# MQTT
Same broker as the Cooler, port 8883, TLS against the same private CA.

| Topic | Direction | Retained | Payload |
|---|---|---|---|
| `dehum/cmd` | panel/app → dehumidifier | no | `{"mode":"timer","timer_h":3}`, `{"mode":"cycle","cycle_on":20,"cycle_off":40}`, `{"min_run":15}` |
| `dehum/want` | Cooler → dehumidifier | no, QoS 0 | `{"want":1}` on change and every 30 s |
| `dehum/data` | dehumidifier → all | yes | every 30 s and on change, below |
| `dehum/availability` | dehumidifier | yes | `online` / `offline` (LWT) |

`dehum/data`: `v` (1), `mode`, `relay`, `want` (null if none in 90 s),
`link_lost`, `hold_s` (min run left), `remain_s` (timer left, or cycle phase
left), `phase` (`on`/`off`, cycle only, else null), `temp` and `humidity`
(local sensor, null if no reading in 30 s), `humid`, `dehum_rh`, `min_run`,
`timer_h`, `cycle_on`, `cycle_off`, `run_s`, `uptime_s`.

| Login | Used by | Broker ACL |
|---|---|---|
| `dehum` (`dehum_mqtt_username`) | the dehumidifier | read/write `dehum/#` |
| controller login | the Cooler | + write `dehum/want` |
| panel and app logins | CoolerPanel, CoolerApp | + read `dehum/data`, `dehum/availability`; write `dehum/cmd` |

Passwords live only in the git-ignored `dehumidifier/secrets.yaml`
(keys in `secrets.yaml.example`).

# Build
Host tests: `cd dehumidifier && cmake -S tests -B build && cmake --build build -j && ctest --test-dir build`.
Firmware (ESPHome 2025.2): `esphome run dehumidifier/dehum-v1.yaml`.

# Install checklist
Bench first, with the relay switching a lamp or meter, not mains:
1. Relay polarity: `on` closes COM/NO. Relay stays open through power-up, reset, OTA and a crash.
2. Each mode via `mosquitto_pub` to `dehum/cmd` behaves as in Modes; watch `dehum/data`.
3. In auto, stop the Cooler's `dehum/want` messages: relay opens after 90 s (plus any `min_run`).
4. Block the broker / WiFi in `on`: relay opens after 60 s, closes again on reconnect.
5. Power up in `on` with the broker unreachable: relay stays open.
6. With the live Cooler: auto follows AC rest / cool, and only runs when local RH ≥ `dehum_rh`.
Then wire the hot line through COM/NO with the fuse, in a closed box.
