# CoolerApp Humidity Tab + Typed Setting Entry — Design

Date: 2026-10-28. Status: draft for the user's review.

## 1. Intent

The dehumidifier controller (`RULES-dehum.md`, spec
`2026-10-28-dehumidifier-controller-design.md`) is on the bench and talks MQTT
(`dehum/cmd`, `dehum/data`, `dehum/availability`). The user wants to watch and
drive it from the Android app (`CoolerApp/`), the same way they watch and drive
the Cooler: a graph of the last hours with the hourly average, and the controls
beside it. The wall panel (CoolerPanel LCD) gets its dehumidifier screens in a
later round; this design covers the app, a Node-RED history flow, and the
broker ACL lines they need.

While at it, stepping a value one +/- tap at a time (each waiting for the
device to confirm) is slow for big changes, so every number setting — on the
new tab **and** on the existing Cooler Settings page — also gets a typed entry.

## 2. Goals / non-goals

Goals
- A fourth bottom-bar tab **Humidity** (water-drop icon, Material
  `Icons.Filled.WaterDrop`), holding two pages switched at the top: **Graph**
  and **Controls**.
- Graph page mirrors the Cooler Status page's trend, for the dehumidifier's own
  humidity sensor.
- Controls page sets every `dehum/cmd` field.
- Typed entry for every number setting, on both settings pages.
- 24 h history for the dehumidifier from Node-RED, like `cooler/history`.

Non-goals
- No change to the dehumidifier firmware or to the Cooler firmware (the live
  Cooler update is on hold; the user will say when).
- No LCD (CoolerPanel) work.
- No Cooler box humidity or temperature on the Humidity graph (the user did not
  ask; easy to add later).
- No confirmation dialogs on mode buttons (same as Cooler settings today).

Anti-goals
- The app must not break when the dehumidifier, its topics or its history are
  absent: the Cooler tabs keep working exactly as now.
- A typed value must never send something out of range.

## 3. Graph page

Top readout (like the Status page's big numbers):
- Current humidity (`dehum/data.humidity`, %RH, no decimals) and the 1-hour
  rolling average beside it, both large; the average in the average line's
  colour. Same averaging as `TrendAverage` (time-weighted, gaps count for
  nothing, needs ≥ 90 % of the hour on record, "-.-" otherwise), applied to
  humidity instead of temperature.
- Status line: mode (`AUTO`, `ON`, `OFF`, `TIMER 1 h 23 m left`,
  `CYCLE run 12 m left` / `CYCLE rest 4 m left`), relay on/off, and for AUTO
  whether the Cooler's want is present (`AC resting` / `AC cooling` /
  `no Cooler signal`) and whether it is humid. `link_lost` or offline shown as
  on the Cooler page.

Trend chart (same component style as `TrendChart`):
- Humidity line, rolling 1-hour average line, dashed horizontal line at
  `dehum_rh` (the AUTO threshold), shading where the dehumidifier relay was on.
- Same zoom buttons 1h / 3h / 6h / 12h, default 3h; window mean for the span on
  screen, as on the Status page.
- Data: live `dehum/data` samples while the app is open, merged with
  `dehum/history` (section 6) when it arrives. A sample with `humidity: null`
  is a gap. Timestamps are the phone's clock on receipt, as for the Cooler.

## 4. Controls page

Mode row: five buttons ON / OFF / TIMER / AUTO / CYCLE; the current mode
(`dehum/data.mode`) highlighted. A tap publishes `{"mode":"<name>"}` at once.
Tapping TIMER or CYCLE while already in it restarts it (firmware behaviour).

Number rows (each − value +, tap the value to type, see section 5):

| Key | Label | Unit | Range | +/- step |
|---|---|---|---|---|
| `timer_h` | Timer | h | 1 .. 24 | 1 |
| `cycle_on` | Cycle run | min | max(1, `min_run`) .. 240 | 5 |
| `cycle_off` | Cycle rest | min | 1 .. 240 | 5 |
| `min_run` | Min run | min | 0 .. 60 | 1 |
| `dehum_rh` | AUTO above | %RH | 30 .. 95 | 1 |

Each change publishes `{"<key>":<int>}` to `dehum/cmd` (never retained) and is
pending until a live `dehum/data` reports the value, reverting after 3 s
without one — the existing `PendingCommands` behaviour. Raising `min_run` above
`cycle_on` raises `cycle_on` too (firmware); the app just shows what comes
back. Changing `timer_h` or the cycle minutes while that mode runs changes the
running mode's lengths without restarting it (firmware behaviour; lowering
`timer_h` below the elapsed time ends TIMER at once and returns to AUTO — the
hint under the timer row says so while TIMER runs).

Offline (no `dehum/data`, `dehum/availability` = `offline`, or the broker
down): every control disabled and a banner, as the Cooler Settings page does.

## 5. Typed entry (both settings pages)

- Tapping a stepper's value replaces it with a numeric text field (number
  keypad) and a **Send** button. Send or the keypad's Enter publishes the value
  through the same path as a +/- tap (pending, confirm, revert).
- Whole numbers only (a leading minus allowed where the range goes below 0,
  e.g. Ice cutoff). Send is disabled while the text is empty, not a number, or
  outside the row's range; a hint under the field shows the range ("1–240").
  The range is the live one (e.g. `cycle_on` floor follows `min_run`; Ice clear
  stays above Ice cutoff, via `Bounds.floor`).
- A typed value need not sit on the +/- step grid (e.g. Min run 45 s is fine).
- Tapping elsewhere, Back, or opening another row's field closes it without
  sending. One field open at a time.
- The Cooler's `sampleinterval` stays a preset row (no typed entry).
- Disabled whenever the row's +/- buttons are disabled.

## 6. Node-RED: `dehum/history`

A second function on the same pattern as `nodered/history.js`: one-minute
slots, 1440 slots (24 h), flow memory, republished after every live
`dehum/data`, not retained, ignoring retained copies and data while
`dehum/availability` is `offline`. Payload:

```json
{"v":1,"t0":<epoch s>,"interval_s":60,"hum":[…],"temp":[…],"relay":[0|1|null,…]}
```

`hum` and `temp` from `humidity`/`temp` (null when the reading is null),
`relay` from `relay`. The app ignores anything that is not a sound v1 window,
as `HistoryParser` does. Lives in `nodered/` with its own test and README
section; the builder decides whether it is a second node on the existing tab or
its own flow file.

## 7. Broker ACL (champion, cooler blocks only, with the user's go-ahead)

- Node-RED's login: + read `dehum/data`, `dehum/availability`; + write
  `dehum/history`.
- App login (`coolerapp`): + read `dehum/history` (it already reads
  `dehum/data`, `dehum/availability` and writes `dehum/cmd`).
- Panel login: + read `dehum/history` too, ready for the LCD round.

The app subscribes to `dehum/data` and `dehum/availability` as **optional**
subscriptions (like `cooler/history` today): a broker that refuses them leaves
the Humidity tab showing "no dehumidifier" and the rest of the app unaffected.
Remember the Mosquitto gotcha: a refused read may still be SUBACKed and then
silently dropped, so "no data yet" and "refused" look the same — the tab says
"waiting for dehumidifier".

## 8. Calls made without the user (check these)

- Dehumidifier topics are fixed `dehum/...`, not derived from the app's
  configurable Cooler base (firmware and Node-RED hard-code them).
- The dehumidifier's health is judged on its own: `dehum/data` older than 90 s
  (it publishes every 30 s) or `dehum/availability` = `offline` means offline.
- The dehumidifier history keeps a slot when humidity is present (the Cooler's
  parser keys on temperature).
- "No dehumidifier" only when the broker refuses the subscription; otherwise
  "waiting for dehumidifier".
- Typed Send uses the same debounce and pending path as +/-; mode taps publish
  at once.
- +/- steps: 5 min for cycle run/rest, 1 elsewhere.
- The app login's existing `dehum/data`, `dehum/availability` read and
  `dehum/cmd` write were added in the dehumidifier broker step (2026-10-28);
  the plan re-checks the ACL read-only before relying on it.

## 9. Left to the builder

Composable structure, file names, chart colours (pick from the existing theme),
how the two-page switch looks (tab row or segmented buttons), how the existing
`TrendBuffer`/`TrendAverage`/`TrendChart` are generalised or copied for a
humidity series, and the test split (unit tests for parsing, averaging, range
checks of typed entry; UI tests in the style of the existing androidTest).
README screenshots updated if the screenshot test makes that cheap.
