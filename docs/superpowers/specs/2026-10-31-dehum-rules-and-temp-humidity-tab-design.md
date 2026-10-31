# Dehumidifier rules + Temp/Humidity tab — design

**Status:** agreed in conversation 2026-10-31 (Darryl). Supersedes AUTO in
`2026-10-28-dehumidifier-controller-design.md` §2.2 (the Cooler `want` link) and the
`dehum_rh` threshold.

## Why
AUTO only ran the dehumidifier when the Cooler reported the AC resting and the room was
humid. Darryl wants the box to run on its own sensor, with four general rules that can
drive either relay, and the app to show the box's temperature as well as humidity.

## 1. Firmware (dehumidifier/dehum_logic.h, dehum-v1.yaml, dehum_esphome.h)

**The Cooler link is removed.** No `dehum/want` subscription, no `want` state, no
`WANT_FRESH_MS`, no `want` field in `dehum/data`. (The Cooler may keep publishing it;
nothing listens. Removing it from `controller/cooler-v4.yaml` is a later, separate flash.)

**Four rules.** Each is six integer settings, `N` = 1..4, persisted with the rest:

| Key | Meaning | Bounds |
|---|---|---|
| `rN_en` | 1 enabled, 0 disabled | 0..1 |
| `rN_sensor` | 0 temperature, 1 humidity | 0..1 |
| `rN_cmp` | 0 below, 1 above | 0..1 |
| `rN_value` | threshold, whole °C or %RH | temp −20..50, humidity 0..100 (clamped to the rule's sensor; changing the sensor re-clamps) |
| `rN_relay` | 1 dehumidifier (GPIO16), 2 spare (GPIO23) | 1..2 |
| `rN_action` | 1 turn on, 0 turn off | 0..1 |

Defaults (a fresh or migrated device keeps today's behaviour minus the AC link):
r1 = en 1, humidity, above, 65, relay 1, on. r2 = en 0, humidity, below, 55, relay 1, off.
r3 = en 0, temp, above, 10, relay 2, on. r4 = en 0, temp, below, 2, relay 2, on.

`dehum_rh` is removed (r1 replaces it). The preferences key changes, so a device upgrading
starts from these defaults once (mode AUTO), as the last layout change did.

**Matching (with hysteresis), per rule:** reading x of the rule's sensor, band = 3 %RH or
1 °C. Above: match latches on at x ≥ value, off at x < value − band. Below: on at
x ≤ value, off at x > value + band. A reading older than 30 s (or NaN) = no match and the
latch clears. A disabled rule never matches. Any command that changes a rule's settings
clears that rule's latch, so it is re-judged from the threshold.

**Only in AUTO.** For each relay k: desired = (some enabled, matching rule with relay k and
action on) AND NOT (some enabled, matching rule with relay k and action off). Off wins; no
match = off. ON / TIMER / CYCLE drive relay 1 exactly as today and hold relay 2 off.
OFF = both off at once.

**Protections cover both relays:** off until the broker has connected once; off after 60 s
of broker loss; `min_run` holds each relay on for at least `min_run` minutes once on (OFF
and broker loss cut it short), each relay timed on its own.

**MQTT.** `dehum/cmd` accepts every key above as an integer (same as the other settings;
unknown or non-integer ignored), alongside the existing keys and `mode`. `dehum/data`
stays `v: 1` (Node-RED's history flow checks it), drops `want`, `humid`, `dehum_rh`, and
adds: `relay2` (0/1), `hold2_s`, every `rN_*` setting, and `rN_match` (0/1).
`relay` / `hold_s` / `run_s` stay relay 1's.

## 2. App (CoolerApp)

- Bottom tab "Humidity" → **"Temp/Humidity"**.
- **Its Graph page:** readout `temp / avg °C` and `RH / avg %RH`; chart with temperature
  (left scale), humidity (right scale), 1-h rolling average of each, chips Temp, RH, Avg,
  RH avg; dashed lines at each enabled rule's threshold on its own scale; shading = relay 1
  running. The dehum trend now stores temperature as `Sample.value` and humidity in
  `Sample.humidity` (live and `dehum/history`), the same shape as the Cooler's.
- **Cooler Status graph:** adds the 1-h rolling average humidity line on the humidity scale
  with an "RH avg" chip.
- **Controls page:** Mode chips unchanged (AUTO label "AUTO (rules)"). Settings: Timer,
  Cycle run, Cycle rest, Min run (AUTO above removed). New **Rules** section: four cards,
  each Enabled switch · Temp | Humidity · Above | Below · value (− / value / + with typed
  entry, range per sensor) · Relay 1 | Relay 2 · On | Off. A rule card shows "met" while
  its `rN_match` is 1. Note: "Rules apply only in AUTO. Off wins when rules disagree."
- Status words: AUTO shows which rules are met ("rules met: 1, 3" / "no rule met");
  relay 2's state is shown ("Relay 2 on/off").
- Rule fields reuse the settings machinery (drafts, 400 ms debounce, pending until echoed).

## 3. LCD panel (CoolerPanel) — added 2026-10-31

The panel had no dehumidifier support; it gets parity with the app (§2).

- **Trend page (Cooler):** adds the 1-hour rolling average humidity line on the humidity
  scale, with an "RH avg" toggle beside Temp / RH / Avg (all may be off, as today).
- **New fourth page "Temp/Humidity"** in the nav, with Graph and Controls sub-tabs:
  - Status strip: dehumidifier online / offline / waiting (from `dehum/availability` and
    data freshness, like the app's DehumHealth), and "Broker link lost: off" when
    `link_lost` = 1.
  - *Graph:* `temp / avg °C` and `RH / avg %RH`, a status line (mode words as the app:
    "TIMER 1 h 23 m left", "CYCLE run 12 m left", "AUTO (rules)"; "Relay 1 on · Relay 2 off";
    in AUTO "rules met: 1, 3" / "no rule met"), and a chart with temperature, humidity
    and both averages, toggles Temp / RH / Avg / RH avg, zooms as the Trend page, dashed
    lines at enabled rules' thresholds on their own scale, relay 1 running shaded.
    Live samples from `dehum/data` (temp primary, humidity alongside) plus
    `dehum/history` (Node-RED, same v1 window shape as `cooler/history` but arrays
    `hum`, `temp`, `relay`).
  - *Controls:* mode buttons ON / OFF / TIMER / AUTO (rules) / CYCLE (send at once);
    Timer, Cycle run, Cycle rest, Min run steppers (bounds as §1; cycle run ≥ min run);
    four rule cards: Enabled toggle · Temp | Humidity · Above | Below · threshold stepper
    (range by sensor) · Relay 1 | Relay 2 · Turn on | Turn off, with "met" shown. Edits
    use the panel's existing settings pattern (debounce, pending until echoed, timeout
    message). Controls disabled unless the dehumidifier is online.
- **MQTT:** subscribe `dehum/data`, `dehum/availability`, `dehum/history` (fixed topics,
  not under the Cooler base); publish `dehum/cmd` (never retained). The panel login's
  ACL already allows these (RULES-dehum.md). Cooler alarms are unaffected: the dehumidifier
  being offline is shown on its page, not as a Cooler alarm takeover.
