# Dehumidifier Controller (v1 hardware) — Design

Date: 2026-10-28. Status: for approval.

## 0. Summary

A small plug-in dehumidifier in the walk-in gets its power switched by a relay
on the user's old v1 ESP32 board. A new ESPHome firmware (`dehumidifier/`)
drives the relay and talks MQTT on the same broker as the Cooler. The Cooler
controller (v4) gains one setting and works out, for AUTO mode, whether the
dehumidifier should run. The LCD panel gets dehumidifier screens in a later
round; this round is the dehumidifier firmware plus the Cooler-side changes.

### Why

The dehumidifier and the AC should not normally run at the same time. The
dehumidifier is meant to work while the AC is resting, and only when the box
is humid enough to need it. The user also wants to take manual control from
the LCD (on, off, a timer, or a repeating run/rest cycle) without reflashing
anything.

### Goals

1. Dehumidifier modes: **AUTO, ON, OFF, TIMER, CYCLE** (§2.1).
2. AUTO: on while the Cooler reports the AC resting **and** the dehumidifier's
   own humidity sensor reads RH at or above `dehum_rh`; off otherwise.
3. A dehumidifier min run time, settable over MQTT (and on the LCD later).
4. Safe failure: lost broker means the dehumidifier is off.
5. The MQTT contract is complete enough that the LCD round needs no further
   firmware changes.

### Non-goals (this round)

- LCD panel screens (next round). The Android app is not planned at all yet.
- Node-RED history for the dehumidifier.
- Letting the dehumidifier's min run time delay the AC. The AC never waits
  (a possible future LCD config value).
- Measuring the dehumidifier's current/power or water-tank-full detection.

### Anti-goals

- The relay must never switch on by itself: not at boot, OTA, a crash or
  brown-out, and not in AUTO from a stale command.
- No change to how the Cooler controls the AC. The only Cooler changes are
  a new setting, a computed flag, and one extra publish.

## 1. Hardware

| Part | Detail |
|---|---|
| Board | user's v1 board, ESP32 DevKitC V4 |
| Relay module | 2-channel 5 V relay board rewired to GPIO16 (dehumidifier) and GPIO23 (spare) |
| Dehumidifier relay | **GPIO16**. Contacts COM/NO in the **hot** (live) wire only; neutral and ground pass straight through |
| Other channel | moved by the user from GPIO14 to **GPIO23** (GPIO14 pulses at boot; GPIO12 is a strapping pin that stops boot if pulled high). Unused this round, held at its "off" level by the firmware, kept free for a later use |
| Fuse | 5 A fuse or breaker on the switched line (recommended) |

- The dehumidifier was confirmed to resume running by itself when power is
  restored, so switching its power is enough.
- Using NO means any ESP32 failure leaves the dehumidifier off.
- **Builder must bench-check, before wiring mains:** (a) whether the relay
  inputs are active-low (most 5 V boards are) and set `inverted` accordingly;
  (b) that the relay stays open through power-up, reset and OTA; (c) the
  board's module: if it is a WROVER (has PSRAM), GPIO16 is taken by PSRAM
  and the relay must move to another safe pin. Report that back to the user
  rather than choosing a pin silently.
- HDC1080 humidity sensor (the v1 board's own) on the board's SDA/SCL (GPIO21/GPIO22), I²C 0x40, 10 kHz. (The old v1 config named an SHT30 at 0x44; the fitted board is an HDC1080.)
- No switches or LED are required. (Optional: drive the board's
  status LED if it has a free one; builder's call.)

## 2. Behaviour

### 2.1 Modes

The dehumidifier controller owns the mode and stores it across reboots.

| Mode | Relay | Ends |
|---|---|---|
| AUTO | follows the Cooler's `want` (§2.2) | when another mode is chosen |
| ON | on | when another mode is chosen |
| OFF | off, **immediately** (ignores min run time) | when another mode is chosen |
| TIMER | on for `timer_min` minutes (changed from whole hours on 2026-10-29 at the user's request) | at the end of the time → **AUTO** |
| CYCLE | on `cycle_on` min, off `cycle_off` min, repeating, starting with on | when another mode is chosen |

- ON, TIMER and CYCLE ignore the AC and the RH.
- TIMER after a dehumidifier reboot: it starts its full countdown again
  (no wall-clock tracking). CYCLE after a reboot: starts again at the
  beginning of an "on" phase. Both are the designer's call.
- `cycle_on` can never be set below `min_run`; if `min_run` is raised above
  it, `cycle_on` is bumped up to match.
- The TIMER countdown and CYCLE phase reset whenever the mode command is
  received again (sending TIMER while in TIMER restarts it with the new minutes).

### 2.2 AUTO (revised 2026-10-28: humidity is measured locally)

The user reconnected the v1 board's own humidity sensor (HDC1080), so the humidity half of the
AUTO decision moved to the dehumidifier. The Cooler only reports whether the
AC is resting.

- **AC resting** (Cooler) = the Cooler's relay is open (`relay == 0`). Defrost
  counts as resting. It uses the relay rather than the inferred compressor so
  that the dehumidifier stops as soon as the AC is told to cool. The Cooler
  sends `want = 1` while resting, `0` while cooling.
- **Humid** (dehumidifier) = a fresh reading from the dehumidifier's own HDC1080
  with hysteresis: becomes true at RH ≥ `dehum_rh`, false at RH <
  `dehum_rh − 3`. No reading for 30 s (sensor fault, unplugged) ⇒ not humid.
- AUTO relay = fresh `want == 1` **and** humid. When the Cooler is silent,
  AUTO is off (no humidistat fallback: the user chose AUTO-only).

The Cooler publishes `{"want":0|1}` to **`dehum/want`**, **not retained**, QoS 0,
whenever it changes and every 30 s regardless, and only while connected.
`cooler/data` carries it as `dehum_want`.

On the dehumidifier side, AUTO relay = the last received `want`, **but only
if a `want` arrived within the last 90 s**; otherwise AUTO treats `want` as 0.
At boot the dehumidifier has no `want` yet, so AUTO starts off.

### 2.3 Min run time

`min_run` minutes: once the relay closes, it stays closed at least this long,
whichever mode turned it on, with two exceptions that open it at once:

1. the user chooses OFF;
2. the broker link has been lost for 60 s (§2.4).

So in AUTO, if the AC starts cooling two minutes into a dehumidifier run, the
AC starts at once and the dehumidifier keeps running alongside it until
`min_run` is up, then stops. The dehumidifier controller enforces this; the
Cooler does not know about `min_run`.

There is no minimum off time: the dehumidifier's own electronics handle its
compressor restart delay. (Designer's assumption; the builder adds nothing.)

### 2.4 Loss of contact

| What is lost | Result |
|---|---|
| Broker (MQTT not connected) for 60 s | relay off in **every** mode, min run time ignored. The mode is kept, and normal control resumes as soon as the link is back |
| Only the Cooler (broker up, no `want` for 90 s) | AUTO acts as `want = 0`. ON / TIMER / CYCLE carry on |

While the broker is lost, TIMER and CYCLE keep counting so they behave
predictably when the link returns (designer's call).

The dehumidifier must not reboot on a lost broker (`reboot_timeout: 0s`, as on
the Cooler), so mode and timers survive an outage.

### 2.5 Startup

Relay off at boot (`restore_mode: ALWAYS_OFF`, explicit turn-off in
`on_boot`). Mode, `timer_min`, `cycle_on`, `cycle_off` and `min_run` are
restored from flash. A restored TIMER or CYCLE restarts per §2.1.
`min_run` does not apply to the boot-off state.

**The relay stays off after boot until the broker has connected for the first
time** (user's decision). Until then every mode reads as off, and
TIMER/CYCLE do not start counting. On first connect the restored mode takes
effect (TIMER/CYCLE start fresh at that moment). The 60 s broker-loss grace
in §2.4 only applies to losing a link that was up; it is not counted from
boot. Bench test: power up with the broker unreachable in ON mode, the relay
must stay open indefinitely.

## 3. Settings

### 3.1 Dehumidifier (`dehum/cmd`, persisted, clamped)

| Key | Unit | Bounds | Default |
|---|---|---|---|
| `min_run` | min | 0 .. 60 | 10 |
| `timer_min` | min | 1 .. 1440 | 120 |
| `cycle_on` | min | max(1, `min_run`) .. 240 | 30 |
| `cycle_off` | min | 1 .. 240 | 30 |
| `dehum_rh` | %RH | 30 .. 95 | 65 (AUTO humidity threshold, local HDC1080) |

Defaults and bounds are the designer's call. Non-integer values are ignored,
as on the Cooler.

### 3.2 Cooler

No new Cooler settings (the first draft's Cooler-side `dehum_rh` moved to the
dehumidifier). The Cooler's `Settings` record is untouched.

## 4. MQTT contract

Same broker, port 8883, TLS against the same private CA as the Cooler.
Everything under `dehum/`.

### 4.1 `dehum/cmd` (JSON, never retained)

Any combination of keys in one message:

```json
{"mode":"auto"}                       // auto | on | off | timer | cycle
{"mode":"timer","timer_min":90}
{"mode":"cycle","cycle_on":20,"cycle_off":40}
{"min_run":15}
```

### 4.1a `dehum/want` (JSON, never retained, QoS 0)

`{"want":0|1}` from the Cooler only, for AUTO. A separate topic so the ACL
can make sure only the `cooler` login can write it; the panel and app cannot
turn the dehumidifier on in AUTO by mistake. (Designer's call.) The
dehumidifier uses a clean session so no queued `want` arrives after a
reconnect.

Settings in the same message apply before the mode, so the TIMER/CYCLE
started by that message uses the new values. Unknown `mode` strings are
ignored.

### 4.2 `dehum/data` (JSON, retained, every 30 s and on any change)

```json
{
  "v": 1,
  "mode": "cycle",          // auto | on | off | timer | cycle
  "relay": 1,                // dehumidifier powered
  "want": 0,                 // last Cooler want, null if none within 90 s
  "link_lost": 0,            // broker-loss lockout in force (only ever seen as 0 live; kept for completeness/after reconnect)
  "hold_s": 412,             // seconds of min run time left, 0 if none
  "remain_s": 1200,          // TIMER: seconds left; CYCLE: seconds left in this phase; else 0
  "phase": "on",             // CYCLE only: on | off; null otherwise
  "temp": 4.1, "humidity": 71.0,   // local HDC1080, null if stale
  "humid": 1, "dehum_rh": 65,
  "min_run": 10, "timer_min": 120, "cycle_on": 30, "cycle_off": 30,
  "run_s": 412,              // seconds in the current relay state
  "uptime_s": 86400
}
```

### 4.3 `dehum/availability`

`online` / `offline`, retained, as birth / LWT.

### 4.4 Cooler additions

- `cooler/data` gains `dehum_want`.
- The Cooler publishes `{"want":…}` to `dehum/want` (§2.2).

### 4.5 Broker logins and ACL (host `champion`, `/etc/mosquitto/acl`)

| Login | Access |
|---|---|
| new `dehum` login (controller) | readwrite `dehum/#` (reads `dehum/cmd` and `dehum/want`) |
| `cooler` (existing) | add write `dehum/want` |
| `coolerpanel`, `coolerapp` (existing) | add read `dehum/data`, `dehum/availability`; write `dehum/cmd` |

Back up the ACL before editing, edit only the cooler blocks (the broker is
shared), `systemctl reload mosquitto`. Note: this Mosquitto acknowledges a
subscription even when the ACL denies it and then drops the messages, so
"subscribed but nothing arrives" means a missing `topic read` line. The new
password goes in the git-ignored `secrets.yaml`, with its key added to
`secrets.yaml.example`. Editing the live broker is done with the user's go-ahead.

## 5. Implementation structure

Mirror the Cooler's split:

- `dehumidifier/dehum-v1.yaml`: wiring and I/O only (WiFi, MQTT/TLS, relay,
  1 s control tick, publish, persistence).
- `dehumidifier/dehum_logic.h`: pure C++17 `step(inputs, settings, state,
  now_ms) → outputs`, no ESPHome dependencies. Uses saturating per-tick time
  accumulation like `cooler_logic.h`, so `millis()` wrap can't break timers.
- `dehumidifier/tests/`: doctest host tests (same CMake/FetchContent setup as
  `controller/tests`).
- `dehumidifier/secrets.yaml` (git-ignored) + `secrets.yaml.example`, or the
  builder may share `controller/secrets.yaml` via `!include`/symlink if
  simpler (builder's call; it must stay git-ignored).
- `RULES-dehum.md` at the repo root: hardware, modes, settings, MQTT, install
  checklist, in the style of `RULES-v4.md`.
- Cooler: `dehum_want` = relay open, computed and published in the YAML
  (`dehum/want`); no Cooler logic or settings change.

## 6. Testing and verification

Host tests (dehumidifier) must cover at least: each mode's relay result;
TIMER expiry → AUTO; CYCLE phases and restart on re-send; `min_run` holding
the relay on through `want` 1→0 and ending on time; OFF and broker-loss
cutting through `min_run`; the 60 s broker grace and recovery; `want`
expiry after 90 s; `cycle_on` ≥ `min_run` clamping; settings clamping;
AUTO needs local humid; RH hysteresis; stale/absent RH blocks AUTO; ON /
TIMER / CYCLE ignore RH.

Cooler: existing tests must keep passing; firmware must compile.

Bench, before mains (relay driving a lamp or meter only):

1. Relay stays open through power-up, reset, OTA, and a crash.
2. Each mode via `mosquitto_pub` behaves per §2.1.
3. Stop the Cooler's publishes (or block it): AUTO drops after 90 s.
4. Stop the broker / WiFi: relay opens after 60 s in ON; returns on reconnect.
5. With the live Cooler: dehumidifier follows AC rest/cool and `dehum_rh`.

Flashing the live Cooler and editing the live broker are done only with the
user's go-ahead.

## 7. Left to the builder

- ESPHome board id for the DevKitC V4 and framework (esp-idf, as the Cooler,
  for TLS with `certificate_authority`).
- Exact debounce/tick details, as long as the timings above hold.
- Whether to share the Cooler's CA/secrets files or copy them.
