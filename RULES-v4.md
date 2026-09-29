# Walk-In Cooler Controller v4 — Rules

One Frigidaire window AC cools the walk-in. The ESP32 (WROOM-32U) never switches
compressor power: one relay swaps the resistance on the AC's room-thermistor
connector (CN3) so the AC's own board sees a "warm" room (13.6 kΩ ≈ 18 °C, cool) or a
"cold" room (35 kΩ ≈ −1 °C, stop). The Frigidaire keeps its own compressor protection.

Implemented in `controller/cooler-v4.yaml` + `controller/cooler_logic.h`.
Full design: `docs/superpowers/specs/2026-09-26-single-relay-controller-design.md`.
The v3 dual-AC rules remain in `RULES.md` for reference. The wall panel and the
Android app are companion projects (`CoolerPanel/`, `CoolerApp/`) that speak the MQTT
contract below.

# Hardware
| Part | Pin | Notes |
|---|---|---|
| SHT30 box sensor | SDA 33 / SCL 22 | I2C 0x44 |
| Fin thermistor (the AC's original, moved into the coil fins) | GPIO35 | 3.3 V → 32 kΩ (22 k + 10 k) → GPIO35 → thermistor → GND |
| Cooling relay input (JD-VCC jumper removed) | GPIO16 | low = closed = "warm" = cool. Its contacts are the COM2/NO2 pair that feed the terminal block (bench-verified 2026-09-28) |
| Other relay input (unused) | GPIO19 | held HIGH = off by the firmware |
| Override switch (latching, to GND) | GPIO21 | closed = override |
| Status LED | GPIO32 | see below |

Header pins 5, 12, 14, 17, 18, 23, 25, 26 and 27 are not usable on this carrier board:
never use them (GPIO12 can also stop the board booting; GPIO14 pulses at boot).

CN3 (as built 2026-09-28): 35 kΩ always across the pins; 22 kΩ in series with relay
COM/NO across the same pins. Relay open: 35 kΩ (AC reads ≈ −1 °C). Relay closed:
35 ∥ 22 = 13.6 kΩ (AC reads ≈ 18 °C). Readings assume the AC thermistor is a
10 kΩ-at-25 °C NTC, which matches its bench reading (10.5 kΩ at 23 °C). Nothing on the CN3 side touches the ESP32.

**AC-side terminal block** (6 positions, left to right), wired to the cooling relay's COM2/NO2:

| Block | Label | Connects to |
|---|---|---|
| 1 | thermistor | AC CN3 lead (node A) |
| 2 | thermistor | AC CN3 lead (node B) |
| 3 | resistor high | node A (links to 1 and 5) |
| 4 | resistor high | node B (links to 2 and relay NO2) |
| 5 | resistor low | node A |
| 6 | resistor low | relay COM2 |

Two stages. NC2 and relay 1's contacts stay empty in both.

1. **Find the values with pots.** A 50 kΩ pot in 3–4 (resistor high, R_COLD) and one in 5–6
   (resistor low, R_ADD), each with its wiper tied to one end. With the AC unplugged and 1–2
   disconnected, set 3–4 to your cold value (≈35 kΩ) and 5–6 to ≈14 kΩ; never power the AC with
   either pot near 0 Ω. Tune on the AC until it starts reliably with the relay on and stops with it
   off, then (AC unplugged, 1–2 disconnected, relay off) meter 3–4 and 5–6 and write them down.
2. **Fit the fixed resistors.** Replace each pot with one resistor of the value metered across
   its two positions (nearest 1 % value, or two in series). Re-check 1–2 with the relay off and on.

**Frigidaire setup:** Cool mode, fan **continuous**, setpoint at its **minimum (≈16 °C / 61 °F)**.
The setpoint must sit between the two fake temperatures (≈ −1 °C and ≈ 18 °C). If the AC's
minimum is above ≈17 °C, fit 14 kΩ (6 k + 8 k) in the low slot instead: closed becomes
10.0 kΩ (≈25 °C) and a ≈22 °C setpoint works.

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
1. **Icing (defrost)** — fin ≤ `fin_cutoff` (default 1 °C): relay opens regardless of
   box temperature; the fan melts the ice. Ends at fin ≥ `fin_recover`
   (+3 °C) or when the fin has matched box temperature (±1 °C) for 2 min.
2. **Fin sensor failed** — timed duty (`dutypercent` of `maxrun`) replaces
   icing protection until the sensor recovers.
3. **`minruntime`** — relay stays closed at least this long (defrost may cut it).
4. **`minofftime`** — relay stays open at least this long, counted from boot.

There is no duty cycle during a pull-down: the AC runs flat out while the coil
stays above `fin_cutoff`, and defrost breaks start by themselves as the box gets cold.

# Settings (MQTT `/cmd`, persisted, clamped)
| Key | Unit | Bounds | Default |
|---|---|---|---|
| `coolerset` | °C | 2 .. 40 | 4 |
| `range` | °C | 0 .. 5 | 2 |
| `fin_cutoff` | °C | −5 .. 5 | 1 |
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
Your broker (`mqtt_broker` in `secrets.yaml`), port **8883, TLS** (verified against the private LLMMon CA).
Topics under `cooler/`: `cooler/data` (retained, `"v": 2`) every 30 s and on any change;
`cooler/availability` online/offline (LWT); `cooler/cmd` as above. The override switch
is also published as `cooler/binary_sensor/override_switch/state` (ON/OFF).

| Login | Used by | Broker ACL |
|---|---|---|
| controller login (`mqtt_username`) | the controller | read/write `cooler/#` |
| panel login (`panel_mqtt_username`) | the LCD panel (CoolerPanel) | read `cooler/data`, `cooler/availability`; write `cooler/cmd` |
| app login (`app_mqtt_username`) | the Android app (CoolerApp) | same as the panel's |

Passwords live only in the git-ignored `controller/secrets.yaml` (keys listed in `secrets.yaml.example`). Field list: spec §3.1.

The SHT30 runs at I²C 10 kHz: at 100 kHz it did not answer on its cable (4.7–10 kΩ
pull-ups on SDA/SCL add margin).

# Fin calibration
Points are only taken while cooling is paused (relay open ≥ `settle` min,
compressor stopped, both readings steady), so a pull-down never yields any.
Procedure, with the box **empty**:

1. Let the first pull-down bring the box to set point.
2. Pause cooling: set `coolerset` to 40 (the relay stays open, the fan runs).
3. Send `{"calibrate":1}`. As the box warms back toward room temperature the
   controller collects (fin resistance, SHT30 temperature) points and fits the
   thermistor's Beta once it has ≥ 4 points spanning ≥ 8 °C.
4. When `/data` shows `cal_active: 0`, restore `coolerset`.

`fin_cal: 1` and `fin_cal_err` < 0.5 °C mean done. A run that has not
finished after 48 h gives up and keeps the previous values.

# Install checklist
1. AC unplugged: CN3 network reads ≈35 kΩ (relay open) / ≈13.6 kΩ (closed); no
   continuity from either CN3 lead to ESP32 GND or 3.3 V.
2. Relay stays open through power-up, reset, OTA and a crash.
3. A 10 kΩ pot in place of the fin probe sweeps `fin_temp`; open and shorted
   raise `fin_fault` within ~10 s.
   Unplug the SHT30: `temp` goes `null` within 30 s and `sht_fault` sets within ~330 s.
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
7. Run calibration after the first pull-down (see "Fin calibration").
8. Unplug the AC with the relay closed: `no_response` fires after 10 min.
