> **Note:** this file describes the retired v3 dual-AC controller. The current controller is documented in [RULES-v4.md](RULES-v4.md).

# Walk-In Cooler Controller — Rules

Two AC units cool one walk-in box. Each unit's fan runs continuously; the ESP32
switches only the condensor on each unit, via one relay each. Only ever one
condensor runs at a time — they alternate — so each coil gets a long fan-only
period to shed ice.

Implemented in `configc32-dual-v3.yaml`.

# Hardware
- 2 relays, one for each AC unit condensor (relay_1 = AC1 / GPIO23, relay_2 = AC2 / GPIO16)
- 1 LCD screen (SSD1306 128x64, I2C 0x3C)
- 2 buttons (temp up / temp down)
- potentiometer (duty cycle percent, GPIO35 ADC)
- physical override button (GPIO19)
- SHT30 temperature / humidity sensor (I2C 0x44)

# Variables
All are persisted and revert to their last used value on boot. All can be
get/set over MQTT. Every write is clamped to the stated bounds.

| Name | Unit | Bounds | Default | Notes |
|---|---|---|---|---|
| `coolerset` | °C | 2 .. 40 | 4 | target box temperature; also set by the up/down buttons |
| `range` | °C | 0 .. 5 | 2 | hysteresis band either side of `coolerset` |
| `maxrun` | minutes | > `minofftime`, <= 60 | 10 | how long one AC runs before cycling to the other |
| `minofftime` | minutes | 0 .. 30 | 5 | rest a condensor must get before it may restart |
| `dutypercent` | % | 1 .. 100 | 50 | % of `maxrun` the condensor is on for |
| `minruntime` | seconds | 0 .. 600 | 180 | floor on a single run once energised; 0 disables |
| `screentimeout` | seconds | 0 .. 3600 | 300 | blank the LCD after this long idle; 0 = never blank |
| `sampleinterval` | seconds | 10 .. 3600 | 3600 | spacing between temp/humidity history samples |

- `maxrun` must be strictly greater than `minofftime`. A set that would violate
  this bumps `maxrun` to `minofftime + 1`.
- The potentiometer maps 0 .. 3.145 V to 1 .. 100 %. More knob = more cooling.

# Rules

## Modes
Exactly one mode is in force at any time. Override takes precedence over SHT30
failure mode.

| Mode | When | duty from | `maxrun` | `minofftime` | Cooling demand |
|---|---|---|---|---|---|
| override | override button on | **pot** | 15 | 14 | always on |
| sht30-failsafe | SHT30 failed | **pot** | 30 | 15 | always on |
| normal | otherwise | `dutypercent` | `maxrun` | `minofftime` | thermostat |

- The potentiometer supplies the duty cycle **only** in override and failsafe
  modes. In normal mode duty comes from `dutypercent` over MQTT.
- Override runs all the time because with the override engaged there is no
  temperature reference to decide when to stop.
- SHT30 is considered failed after 300 s with no valid reading. It auto-recovers
  on the next valid reading.
- The effective values are re-resolved every second, so a mode change takes
  effect immediately.

## General
- When temperature goes above the cooler set temperature (+ `range`), cooling
  turns on.
- When temperature goes below the cooler set temperature (- `range`), cooling
  turns off.
- If the SHT30 reading is invalid but the fault timer has not yet expired, the
  last cooling demand is held.

## Duty cycle and alternation
- Time is divided into windows of `maxrun` minutes. One AC unit owns each
  window; ownership alternates AC1 -> AC2 -> AC1 ...
- `maxrun` is clock time, not run time. The window ends after `maxrun` minutes
  regardless of how much of it the condensor actually ran.
- Within its own window, the owning unit's condensor runs contiguously from the
  start of the window for `dutypercent` % of the window, then rests.
- The non-owning unit's condensor is off for the whole window. Both condensors
  are never energised at the same time.
- Global cooling duty therefore equals `dutypercent`, while each individual unit
  runs for only half that — which is what keeps the coils clear.
- Changes to `maxrun`, `dutypercent` or the mode apply immediately. If the new
  `maxrun` is shorter than the current position in the window, the window ends
  at once and the lead alternates.

## Compressor protection
- `minofftime` — a condensor that has been switched off may not restart until it
  has been off this long. Because `maxrun` > `minofftime` is enforced, a unit's
  off period is always at least `maxrun`, so this never fires in steady state.
  It exists to cover boot, mode changes, and demand transitions.
- `minruntime` — once energised, a condensor stays on for at least this long. If
  `dutypercent` x `maxrun` is below `minruntime`, it runs `minruntime` and
  forfeits the remainder of the window. Without this, `dutypercent: 1` at
  `maxrun: 60` would produce 36-second compressor runs.
- Hard interlock: if both units are ever asked to run at once, whichever is
  already running wins and the other is refused.

## Startup and persistence
- All variables are kept in memory and revert to their last used value on boot.
- On boot both relays start off. Nothing energises a relay from a boot event.
- `minofftime` is enforced from power-up, so no condensor can start within
  `minofftime` of the controller coming up.
- The lead AC alternates from one boot to the next, so repeated restarts do not
  favour one unit.

# Communications
Topic prefix `ha/esp32-cooler`.

## Status — `ha/esp32-cooler/data`, retained
Published every 30 seconds, and immediately on any change of relay state, lead
unit, mode, or setting.

Required fields: cooler temp, humidity, cooler set temp, `range`, `maxrun`,
`minofftime`, `dutypercent`, AC1 status, AC2 status, general AC on (either unit,
regardless of which or of duty), in duty (either unit).

- AC status is one of `On` (condensor energised), `Duty` (this unit owns the
  window but the duty cycle currently has it off), `Off` (does not own the window).
- Also published: `mode`, `lead`, `active_unit`, `cool_call`, `override`,
  `sht_fault`, `pot_pct`, `minruntime`, `window_s`, `window_pos_s`,
  `window_on_s`, `swap_in_s`, per-unit relay / blocked / runtime, `uptime_s`.

### Temperature / humidity history
Two arrays of at most 20 integers each, carried in the same `/data` object.

    "hist_n": 20, "hist_interval_s": 3600, "hist_last_ts": 1785790000,
    "temp_hist": [5,5,4,4,5,6,5,4,4,4,5,5,4,4,4,5,5,4,4,4],
    "hum_hist":  [78,79,79,80,81,80,79,78,78,79,80,80,79,79,78,78,79,79,80,80]

- One sample every `sampleinterval` seconds. At the default 3600 that is 20
  hours of trend.
- Index 0 is the **oldest** sample, the last entry is the newest. Once 20
  samples are held the window slides — oldest drops off, newest appends.
- `hist_n` is how many samples are actually held, growing 0 -> 20 after a cold
  start. Consumers must read `hist_n` rather than assuming 20.
- Values are rounded to the nearest integer, not truncated.
- A sample is only taken when both readings are valid. A sensor dropout leaves
  a gap in **time**, not a run of zeros in the series.
- `hist_last_ts` is the epoch of the newest sample. Sample *i* was taken at
  `hist_last_ts - (hist_n - 1 - i) * hist_interval_s`, which only holds if the
  controller was up continuously — compare against `uptime_s` to spot a gap.
- Both arrays and `hist_n` are persisted, so a reboot or an OTA does not throw
  away the trend. At one sample an hour that is roughly 24 flash writes a day.
- `{"clearhist":1}` on the command topic resets both arrays.

## Per unit — `ha/esp32-cooler/ac1` and `/ac2`, retained
`status`, `relay`, `lead`, `blocked`, `runtime_s`, `off_for_s`.

## Availability — `ha/esp32-cooler/availability`, retained
`online` on connect, `offline` via MQTT last will, so a dead controller is
distinguishable from a cooler that is simply not calling for cooling.

## Commands — `ha/esp32-cooler/cmd`
JSON. Any subset of: `coolerset`, `range`, `maxrun`, `minofftime`,
`dutypercent`, `minruntime`, `screentimeout`, `sampleinterval`, `override`, plus
`wake` to turn the LCD back on and `clearhist` to reset the history arrays.
Values are clamped to their bounds on receipt, then the control logic re-runs
immediately.

    {"maxrun": 20, "dutypercent": 40}

# Display
SSD1306 128x64, five 12 px rows. Everything is one size — nothing is emphasised
over anything else, and no value is hidden behind a page flip.

## Normal
    4.8°     set 4    +-2
    78%RH    duty 50%
    >AC1 ON   AC2 --
    off in 3m    sw 6m
    norm     max10 off5

- `>` marks the AC unit that owns the current window. `ON` / `--` is the actual
  relay state, so ownership and energised are separately visible.
- `wt` in place of `--` means that unit wants to run but `minofftime` is holding
  it back.
- `off in Nm` counts down to the duty cut-out of the running condensor; `sw Nm`
  counts down to the units swapping. At less than a minute both switch to seconds.
- When cooling is not called for, the countdown line reads `idle`; when the lead
  unit is inside its window but past its duty portion, it reads `resting`.
- Override is shown inline as `OVR` in place of `norm`.
- A failed sensor renders as `--.-°` / `--%RH`, never as `nan`.

## SHT30 fault takeover
A sensor fault replaces the whole screen, since it is abnormal and should not be
missable:

    !! SHT30 FAULT !!
    no data 12m
    failsafe cooling
    >AC1 ON   AC2 --
    duty50 max30 off15

## Blanking
- The screen blanks after `screentimeout` seconds of no button press, to limit
  OLED burn-in. Black pixels on an OLED are unpowered, so this genuinely
  preserves the panel.
- Any button press wakes it. A press on a blanked screen **only** wakes it — it
  does not also move the setpoint. The next press adjusts as normal.
- An SHT30 fault forces the screen on and holds it on for the duration of the
  fault, regardless of `screentimeout`.
- `{"wake":1}` on the command topic also wakes it.

The font declares an explicit glyph set. ESPHome's default omits several
characters used here, which would otherwise render as blank boxes.

# Verified behaviour
Simulated second-by-second against the implemented control logic:

| Scenario | Result |
|---|---|
| normal, duty 50 %, maxrun 10, minoff 5 | 5 m on / 15 m off per unit, global 50 % |
| override, pot 100 % | swap every 15.0 m, cooling 100 %, each unit 15 m on / 15 m off |
| sht30-failsafe, pot 50 % | 15 m per AC per 60 m window, global 50 % |
| all of the above | both condensors on simultaneously: **0 seconds**; minofftime blocks in steady state: **0** |
