# CoolerPanel — Design

A wall-mounted 4" touch panel that monitors and configures the ESPHome walk-in
cooler controller over MQTT. It lives indoors, away from the cooler, so its
primary job is showing a temperature/humidity trend and telling you when
something has gone wrong; its secondary job is changing settings without
walking out to the box.

Forked from [LLMMon](../../../../LLMMon), which already runs on the same board.

---

## 1. Hardware

**Waveshare ESP32-S3 Smart 86 Box** — 4-inch 480×480 IPS panel (ST7701S over
parallel RGB), GT911 5-point capacitive touch on I2C, ESP32-S3 @ 240 MHz,
16 MB flash, 8 MB PSRAM. Identical to LLMMon's target, so the entire device
platform layer transfers unchanged.

Layout is specified in a 480×480 coordinate space.

---

## 2. Architecture

Three layers, inherited from LLMMon. The `shared/` layer builds for both the
PC simulator and the ESP32-S3, so the whole UI can be developed without
hardware in the loop.

```
CoolerPanel/
  shared/          portable — builds for PC and ESP32-S3
    platform.h       the seam: time + outbound publish
    model/
      cooler_state.{h,cpp}   parsed /data snapshot
      history.{h,cpp}        PSRAM ring buffer of samples
      mqtt_router.{h,cpp}    topic dispatch
      commands.{h,cpp}       outbound command builder + reconciliation
      bounds.{h,cpp}         single source of truth for value limits
      alarm.{h,cpp}          alarm state machine
    ui/
      screen_trend.cpp       default screen
      screen_settings.cpp    stepper rows
      screen_detail.cpp      read-only diagnostics
      screen_alarm.cpp       full-screen takeover
      screen_boot.cpp        splash            (from LLMMon)
      screen_setup.cpp       captive portal UI (from LLMMon)
      chart.{h,cpp}          trend rendering + downsampling
      nav.{h,cpp}            swipe paging
      theme.{h,cpp}          colour/type tokens (from LLMMon)
    format.{h,cpp}           value formatting (from LLMMon)
  sim/             SDL desktop platform — primary development loop
  device/          ESP32-S3: ST7701S, GT911, WiFi, MQTT, NVS, captive portal
  config/          device config schema + boot logic
  docs/superpowers/specs/
```

### Carried over unchanged
`device/` in full (panel driver, touch, backlight, WiFi, NTP, NVS config,
captive portal), the `sim/` SDL harness, `config/`, `theme.cpp`, `format.cpp`.

### Replaced
All seven LLMMon screens and `app_model` — different domain, no reuse value.

### New capability: outbound MQTT
LLMMon is inbound-only; everything flows in through `app_on_mqtt_message()`.
The panel must send, so the platform seam grows one function:

```c
bool platform_mqtt_publish(const char* topic, const char* payload,
                           size_t len, bool retain);
```

Implemented in `sim/mqtt_mosq.cpp` against libmosquitto and in
`device/src/mqtt_pubsub.cpp` against PubSubClient.

### Consequence of forking
`platform_mqtt_publish`, and any panel-driver or captive-portal fixes made
here, exist only in CoolerPanel unless hand-ported back to LLMMon.

---

## 3. MQTT contract

Broker `mqtt.example.com:1883`, plain (no TLS), matching how Home
Assistant already connects. Client ID `cooler-panel-<chipid>`. Clean session —
retained messages rebuild all state on connect.

| Direction | Topic | Notes |
|---|---|---|
| Subscribe | `ha/esp32-cooler/data` | retained; full state, every 30 s and on change |
| Subscribe | `ha/esp32-cooler/availability` | retained; `online` / `offline` via LWT |
| Publish | `ha/esp32-cooler/cmd` | JSON, any subset of settable keys |

`ha/esp32-cooler/ac1` and `/ac2` are deliberately **not** subscribed. Every
field they carry already exists in `/data`, and two sources for one fact
invites them disagreeing.

The payload schema is defined by the cooler's
[`RULES.md`](../../../../Cooler/RULES.md) — that document is authoritative and
this panel follows it.

---

## 4. Data model

### CoolerState
A plain struct mirroring `/data`:

- **Config:** `coolerset`, `range`, `maxrun`, `minofftime`, `dutypercent`,
  `minruntime`, `screentimeout`, `sampleinterval`
- **Live:** `temp`, `humidity`, `cool_call`, `override`, `sht_fault`, `mode`,
  `lead`, `active_unit`, `pot_pct`, `uptime_s`
- **Per unit:** `ac1`/`ac2` tri-state (`On`/`Duty`/`Off`), `ac{1,2}_relay`,
  `ac{1,2}_blocked`, `ac{1,2}_runtime_s`
- **Window:** `window_s`, `window_pos_s`, `window_on_s`, `swap_in_s`
- **Panel meta:** `last_rx_epoch`, `valid`

Unknown fields are ignored. A malformed or partial payload is dropped and the
last good state retained.

### History
One flat ring buffer in PSRAM. Samples are `{int16 temp_c10, int16 rh_c10}` —
4 bytes, preserving the decimal that the cooler's own integer `hist` arrays
discard. 7 days at 30 s spacing is 20,160 samples ≈ 80 KB.

Zoom levels (1 h / 24 h / 7 d) downsample at render time with min/max per pixel
column. One buffer, no tiers.

**Sampling is time-gated, not message-gated.** `/data` publishes on change as
well as every 30 s, so a burst of relay activity would otherwise over-sample
that period and skew the series. Append only when ≥30 s has elapsed since the
last stored sample.

History is **not** persisted. A panel reboot loses the trend; full current
state rebuilds immediately from the retained `/data`.

If the PSRAM allocation fails, fall back to a shorter window rather than
refusing to boot.

### Bounds table
One table drives both the stepper widgets and the client-side clamp, so the UI
cannot offer an out-of-range value:

| Key | Unit | Min | Max | Step |
|---|---|---|---|---|
| `coolerset` | °C | 2 | 40 | 1 |
| `range` | °C | 0 | 5 | 1 |
| `maxrun` | min | `minofftime`+1 | 60 | 1 |
| `minofftime` | min | 0 | 30 | 1 |
| `dutypercent` | % | 5 | 100 | 5 |
| `minruntime` | s | 0 | 600 | 30 |
| `screentimeout` | s | 0 | 3600 | preset |
| `sampleinterval` | s | 10 | 3600 | preset |

`maxrun`'s minimum is dynamic — it tracks the current `minofftime`.

Two deliberate divergences from the cooler's own bounds:

- **`dutypercent` floors at 5, not 1.** A stepper of 5 starting from 1 gives the
  ugly sequence 1, 6, 11 …, and values below 5 are meaningless anyway once
  `minruntime` forces a floor on the actual run. 1–4 remain reachable over MQTT
  directly.
- **`screentimeout` and `sampleinterval` use preset chips, not steppers.**
  Stepping to an hour at 10 s a tap is absurd. Offer
  `10 s / 1 m / 5 m / 15 m / 1 h` and `off / 1 m / 5 m / 15 m / 1 h`
  respectively.

Every other value is clamped to exactly the cooler's own range, so the panel
and the controller never disagree about what is legal.

---

## 5. Command path and reconciliation

```
tap +1  →  clamp locally against bounds table
        →  debounce 400 ms (coalesce a held button into one publish)
        →  publish {"coolerset":5} to ha/esp32-cooler/cmd
        →  mark field pending, 3 s deadline
        →  cooler clamps, applies, sets dirty, republishes /data
        →  panel reconciles to the authoritative value
```

**The panel never assumes its request took effect.** The cooler clamps every
write: ask for `coolerset: 50` and you get 40; raise `minofftime` above
`maxrun` and `maxrun` silently bumps to `minofftime + 1`. A pending field is
rendered greyed with a spinner and snaps to whatever the next `/data` reports.

No acknowledgement within 3 s reverts the field to its last known value and
raises a brief non-modal toast.

---

## 6. Screens and interaction

Three main screens on horizontal swipe with a page indicator, plus the alarm
takeover. Swipe paging rather than LLMMon's card-overlay nav — for a wall
panel, three pages side by side is a simpler model than modal cards.

### Trend (default)

```
┌─ 480×480 ──────────────────────────────┐
│ 4.8°            set 4 ±2      ◉ 14:32  │  y0-72    temp at 48px
│ ┌────────────────────────────────────┐ │
│ │ ╱╲    ╱╲      ╱╲    ╱╲    ╱╲       │ │  y72-300  graph; setpoint
│ │╱  ╲__╱  ╲____╱  ╲__╱  ╲__╱  ╲__    │ │           band shaded
│ └────────────────────────────────────┘ │
│      [ 1h ]  [ 24h ]  [ 7d ]           │  y300-340 zoom tabs
│  ┌──────────────┐ ┌──────────────┐     │
│  │ AC1   ● ON   │ │ AC2    ○ --  │     │  y340-420 unit tiles
│  │ LEAD  4m→    │ │ standby      │     │
│  └──────────────┘ └──────────────┘     │
│  duty 80%   swap 4m      humidity 78%  │  y420-480
└────────────────────────────────────────┘
```

Unit tiles show lead ownership and actual relay state separately — the same
distinction the cooler's own OLED makes, and the thing that tells you the duty
cycle is working.

### Settings
Stepper rows at 64 px: `label │ value │ − │ +`. Primary group is the six
control values; `screentimeout` and `sampleinterval` sit in an advanced group
below. Override is a toggle rather than a stepper.

### Detail
Read-only: mode, window position, `swap_in`, per-unit runtimes, `blocked`
flags, `pot_pct`, `uptime`, `sht_fault`, broker link state.

### Boot / Setup
Reused from LLMMon, including the captive portal for WiFi and broker setup.

---

## 7. Alarms

Full-screen takeover on any of:

| Condition | Trigger |
|---|---|
| Controller offline | `availability: offline` (LWT) |
| Controller silent | no `/data` for 5 min |
| Sensor failed | `sht_fault: 1` |
| Not keeping up | `temp > coolerset + range + 5` continuously for 60 min |

The temperature threshold is deliberately loose so a door left open or a warm
load going in never nuisance-fires.

Tap acknowledges: the takeover dismisses for a **30-minute hold-off**
(configurable in panel NVS) and leaves a persistent marker in the header. It
re-fires if the condition still holds after the hold-off, or **immediately** if
a different condition arises while one is acknowledged.

Alarm state is per-condition, not global — acknowledging "sensor failed" must
not suppress a subsequent "controller offline".

---

## 8. Panel-local config

Stored in the panel's own NVS, never sent to the cooler:

- Alarm thresholds and hold-off duration
- Backlight level and night dimming
- Default zoom level
- WiFi and broker settings (via the captive portal)

---

## 9. Error handling

| Condition | Response |
|---|---|
| MQTT disconnect | Link glyph changes, auto-reconnect, data marked stale |
| No `/data` for 90 s | Dim + stale indicator; values shown but flagged |
| No `/data` for 5 min | Alarm takeover |
| `availability: offline` | Alarm takeover |
| Command unacknowledged in 3 s | Field reverts, brief toast |
| Malformed / partial JSON | Drop message, keep last good state |
| Panel reboot | History lost by design; state rebuilds from retained `/data` |
| WiFi down | Captive portal takes over |
| PSRAM allocation failure | Shorter history window rather than boot failure |

---

## 10. Testing

The **simulator is the primary development loop** — the full UI runs on the PC
against the live broker. The board is flashed only to confirm hardware.

Unit tests under `ctest`, following the pattern in LLMMon's `config/tests`:

- **Router** — captured `/data` payloads parse into the expected `CoolerState`
- **Bounds** — every value at `lo−1`, `lo`, `hi`, `hi+1`, plus the
  `maxrun`/`minofftime` interaction
- **Command builder** — JSON output shape
- **Reconciliation** — pending→confirmed, pending→timeout-revert, and the clamp
  case where the cooler returns a different value than requested
- **History** — ring wrap, the 30 s time-gate, downsample correctness
- **Alarm state machine** — entry, acknowledge, hold-off expiry, re-fire, and
  immediate fire on a different condition while one is acknowledged

**Replay fixtures.** Capture a real `/data` sequence from the live cooler —
including a lead swap, a duty cut-out, and a settings change — into a fixture
file. Tests then run against real payload shapes rather than invented ones, and
the same fixture drives the simulator when the broker is unreachable.

---

## 11. Suggested build order

The pieces are tightly coupled enough for one implementation plan, but they
sequence naturally into four phases, each independently verifiable:

| Phase | Deliverable | Verified by |
|---|---|---|
| A | Fork the tree, strip LLMMon screens, capture replay fixtures, build `cooler_state` + `mqtt_router` + `bounds` | `ctest` against fixtures; sim boots and parses live `/data` |
| B | `history` + `chart` + Trend screen | Sim renders a live trend against the real broker |
| C | `platform_mqtt_publish`, `commands`, reconciliation, Settings screen | Sim changes a value on the real cooler and reconciles the clamp |
| D | `alarm`, Alarm screen, Detail screen, device flash | Alarm fires on a forced fault; panel runs on hardware |

Phase C is the one carrying real risk — it is the first code in either project
that writes to the cooler. It should be exercised in the simulator against a
harmless value (`screentimeout`) before anything touches `coolerset` or
`dutypercent`.

## 12. Decisions taken

| Decision | Choice | Rationale |
|---|---|---|
| Placement | Indoors, remote monitoring | Trend graph is the centrepiece |
| History source | Panel-accumulated from 30 s `/data` | Full precision and multiple zooms; no cooler firmware change |
| History persistence | None | Reboot loses trend; current state is retained-MQTT anyway |
| Alerting | Visual full-screen alarm with acknowledge | Unmissable in the room, no added hardware |
| Transport | Cooler's broker, plain 1883 | Matches existing setup; no broker work |
| Codebase | Fork to a new repo | Isolation from LLMMon, accepting duplicated plumbing |
| Temp alarm | 5 °C over for 60 min | No nuisance alarms |
| `/ac1`, `/ac2` topics | Not subscribed | Redundant with `/data` |

## 13. Out of scope

- Audible alarm (no speaker on the board as standard)
- Push notification when away — that is Home Assistant's job
- TLS (revisitable; the config path stays capable of it)
- Controlling anything other than this one cooler
- Scheduling or setpoint automation
