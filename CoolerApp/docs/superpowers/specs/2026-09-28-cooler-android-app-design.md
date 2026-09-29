# CoolerApp (Android) — Design

Date: 2026-09-28
Status: approved in conversation, awaiting written-spec review

## 0. Purpose

A phone app to **monitor** the walk-in cooler and **change its settings** from
anywhere, speaking the same MQTT contract as the CoolerPanel wall LCD. One
user (the owner). Sideloaded APK.

Decided in brainstorming:

- Monitoring + control. **No push notifications** — alerts will be built in
  Node-RED later, from the retained `cooler/data` / `cooler/availability`.
- **Recent history only** (controller's 20-sample buffer + live samples while
  the app is open). Long-term history comes from Node-RED later; the trend
  model leaves a seam for it.
- One user, one dedicated broker login (`app_mqtt_username`).
- Native Kotlin + Jetpack Compose, patterned on `~/projects/SPA/spa-android`.
- Override is **not** remote-controllable; the app only displays the physical
  switch position.

## 1. Repo and build

- New folder `CoolerApp/` (sibling of `CoolerPanel/`) in the Cooler repository.
- Package / applicationId `ai.northtrail.cooler`. minSdk 26, compile/target
  SDK 37, Java 11 — same as spa-android.
- Dependencies (same versions as spa-android): Compose BOM 2026.08.00,
  activity-compose 1.13.0, lifecycle-runtime/viewmodel-compose 2.11.0,
  `com.hivemq:hivemq-mqtt-client:1.3.3`, `org.jetbrains.kotlinx:kotlinx-serialization-json:1.9.0`
  (JSON tree API only, for `/data` and `/cmd`; no compiler plugin); tests JUnit 4.13.2,
  kotlinx-coroutines-test 1.9.0, Compose UI test.
- **CA**: `app/src/main/assets/broker_ca.pem`, git-ignored. When present it is
  the **only** trusted CA (spa-android pattern). `tools/import_ca.sh` extracts
  the LLMMon Private CA from `../controller/cooler-v4.yaml`
  (`certificate_authority`) into that file.
- No credentials are ever built into the APK. Defaults baked in: host
  `mqtt.example.com`, port `8883`, topic base `cooler`.

## 2. MQTT contract (consumer side)

| Topic | Dir | Use |
|---|---|---|
| `<base>/data` | sub (retained) | full state, JSON `"v":2`, ~30 s + on change |
| `<base>/availability` | sub (retained) | `online` / `offline` (LWT) |
| `<base>/cmd` | pub | JSON object of settings / actions |

Exactly these two subscriptions; no wildcard. `<base>` defaults to `cooler`.

### `cooler/data` fields used

`temp`, `humidity`, `fin_temp` (number or null) · `mode` (`normal`,
`override`, `fin-proxy`, `override-proxy`, `blind`) · `override_src`
(`none`, `switch`, `link`, `both`) · `state` (`cooling`, `idle`, `wait`,
`rest`, `defrost`) · `relay`, `cool_call`, `defrost`, `sht_fault`,
`fin_fault`, `no_response` (0/1) · `compressor` (1/0/null) · `run_s`,
`off_s`, `hold_s` · the ten settings (§4) · `fin_cal`, `cal_active`,
`cal_points`, `cal_span` · `hist_n`, `hist_interval_s`, `hist_last_ts`,
`temp_hist[]`, `hum_hist[]` · `uptime_s`.

Unknown fields are ignored. Missing or null fields become "unknown" (shown as
`--`), never a crash. A payload whose `v` is not 2 is ignored and logged.

### Commands

| Action | Payload |
|---|---|
| Change a setting | `{"<key>": <int>}` — one key per publish |
| Start calibration | `{"calibrate":1}` |
| Abort calibration | `{"calibrate":0}` |
| Reset fin calibration | `{"fincal_reset":1}` |

`clearhist` is deliberately not offered (the panel doesn't either).

## 3. Broker account

New mosquitto user on the broker, same ACL shape as the panel's login:

```
user <app-login>
topic read cooler/data
topic read cooler/availability
topic write cooler/cmd
```

Password stored as `app_mqtt_username` / `app_mqtt_password` in the
git-ignored `controller/secrets.yaml`, typed into the app once at first run.
Back up `/etc/mosquitto/{passwd,acl}` before editing; reload, don't restart.

## 4. Settings bounds

Mirrors CoolerPanel `shared/model/bounds.cpp` exactly:

| Key | Label | Unit | Min | Max | Step | Group | Widget |
|---|---|---|---|---|---|---|---|
| `coolerset` | Set point | °C | 2 | 40 | 1 | Box | stepper |
| `range` | Range ± | °C | 0 | 5 | 1 | Box | stepper |
| `sampleinterval` | Sample every | s | 10 | 3600 | — | Box | presets 10 s / 1 m / 5 m / 15 m / 1 h |
| `fin_cutoff` | Ice cutoff | °C | −5 | 5 | 1 | Coil | stepper |
| `fin_recover` | Ice clear | °C | max(−4, `fin_cutoff`+1) | 10 | 1 | Coil | stepper |
| `settle` | Settle | min | 2 | 30 | 1 | Coil | stepper |
| `minofftime` | Min off | min | 0 | 30 | 1 | Timing | stepper |
| `minruntime` | Min run | s | 0 | 600 | 30 | Timing | stepper |
| `maxrun` | Max run | min | 1 | 60 | 1 | Timing | stepper |
| `dutypercent` | Backup duty | % | 5 | 100 | 5 | Timing | stepper |

The app clamps locally before sending, but the controller is authoritative:
whatever the next `/data` reports is shown.

## 5. Screens

Bottom navigation with three tabs, plus Setup.

### 5.1 Status (default)

- **Link strip**: `Online · updated 12s ago` / `Controller offline` /
  `Broker unreachable` / `No data for 5 min` / `Connecting…`.
- **Box temperature** (large) and **humidity**; below it
  `set 4 ±2 · on >6 off <2`.
- **State card**: key line = mode label (+ ` - SETTLE` / ` - BACKUP DUTY`),
  value line = state wording (§6).
- **Chips**: switch chip (`SWITCH OFF` / `SWITCH ON` / `LINK LOST`, alert
  colour for the latter two), and when active: `DEFROST`, `BOX SENSOR FAULT`,
  `FIN SENSOR FAULT`, `AC NOT RESPONDING`, `CALIBRATING`.
- **Coil** temperature and **Compressor** wording.
- **Trend chart**: box temperature from `TrendBuffer`, with the
  `coolerset ± range` band shaded and relay-closed spans tinted.
- **Problem banners** (only while the app is open; no acknowledge):

  | Banner | Condition |
  |---|---|
  | Controller offline | availability `offline` |
  | Controller silent | online but no `/data` for 5 min |
  | AC not responding | `no_response` = 1 |
  | Box sensor fault | `sht_fault` = 1 |
  | Fin sensor fault | `fin_fault` = 1 |

  The panel's "Not keeping up" (8 °C over for 6 h) is omitted: the app is not
  open long enough to judge it. It belongs in Node-RED.

### 5.2 Settings

- Grouped Box / Coil / Timing, rows `label · value unit · − · +`,
  `sampleinterval` as preset chips.
- Edits are debounced 400 ms (a burst of taps = one publish; a burst that
  ends on the current value sends nothing), then the row is **pending**
  (greyed + spinner). It is confirmed when a **live** (non-retained) `/data`
  reports the requested value. Otherwise after 3 s it reverts to whatever the
  controller last reported (which may be a clamped value) + snackbar
  "Not applied".
- All controls disabled unless link is Online.

### 5.3 Detail

- Read-only: mode, override source, state, relay, cool call, run / off / hold
  timers, compressor, fin ohms, fin slope, fault flags, uptime, link state.
- **Fin calibration**: status (`fin uncalibrated` / `fin cal ok` /
  `calibrating N pts X.XC`), **Start** / **Abort** button (by `cal_active`),
  **Reset calibration** behind a confirm dialog.
- **Broker settings** → Setup.

### 5.4 Setup (first run, or from Detail)

Fields: host, port, username, password, topic base. **Test & save** connects
with the entered values and waits (10 s) for a `<base>/data` message before
persisting. Errors: `Login refused`, `Can't verify the broker (<reason>)`,
`Can't reach the broker`, `Connected, but no cooler data on <base>/data`.
Password encrypted with an Android Keystore key (spa-android `ConfigStore`)
and never shown again.

## 6. Status wording

Ported from CoolerPanel `shared/model/status_text.cpp`; the app and the panel
must say the same thing. Covered functions: `fmtDur`, `modeLabel`,
`stateValue`, `stateKey`, `compressor`, `finCal`, `switchChip`. Summary:

- `fmtDur`: `<60 s` → `Ns`, `<1 h` → `Nm`, else `Nh MMm`.
- Modes: `NORMAL`, `OVERRIDE`, `FIN PROXY`, `OVR + FIN PROXY`,
  `BLIND TIMER`; invalid → `--`.
- State value: cooling with `hold_s`>0 → `Cooling • min run <hold>`, else
  `Cooling <run>`; idle → `Fan only <off>`; wait → `Waiting <hold>`; rest →
  `Resting <hold>` (bare `Resting` at 0); defrost → `Defrost <off>`.
- State key: mode label; when resting, plus ` - BACKUP DUTY` (fin fault)
  or ` - SETTLE` (a `*proxy` mode without fin fault).
- Compressor: `Running` / `Starting` (relay closed, not yet seen) /
  `Stopped` / `--`.
- Switch chip: `switch`/`both` → `SWITCH ON` (alert), `link` → `LINK LOST`
  (alert), `none` → `SWITCH OFF`, no data → `--`.

## 7. Architecture

```
HiveMqSession ── MqttRepository ── CoolerController ── CoolerViewModel ── Compose UI
```

### `model/` — plain Kotlin, no Android imports, all unit-tested

| Unit | Responsibility |
|---|---|
| `CoolerState` | immutable snapshot of `/data` (nullable fields) + `valid` |
| `CoolerStateParser` | JSON → `CoolerState?` (null for wrong `v` / not JSON) |
| `StatusText` | §6 |
| `Bounds` | §4 table, `clamp(key, v, finCutoff)` |
| `Commands` | payload builders (§2) |
| `PendingCommands` | per-key pending value + deadline; resolve on `/data`, expire at 3 s |
| `Health` | link state + banner list from (connection, availability, last `/data` time, state, now) |
| `TrendBuffer` | samples `(epochS, tempC, relay)`; seeds from `temp_hist` dated back from `hist_last_ts` at `hist_interval_s`; appends live `/data`; de-duplicates by timestamp; capped (24 h at 10 s = 8640). Exposes `samples(sinceS)`; a later `HistorySource` (Node-RED) can merge in |
| `BrokerConfig` | fields + validation (host non-empty, port 1..65535, base has no `#`/`+`/leading `/`) |
| `Topics` | `<base>/data`, `<base>/availability`, `<base>/cmd` |

### `data/`

- `HiveMqSession`: HiveMQ MQTT 3.1.1 async client, TLS; trust manager built
  from `broker_ca.pem` when present, else system trust. Maps failures to
  `AuthFailed`, `TlsFailed(reason)`, `Unreachable`.
- `MqttRepository`: owns the session; connect / subscribe / publish; exposes
  `Flow<Incoming>` and `StateFlow<Connection>`. Backoff 1 s doubling to 60 s.
  `AuthFailed` stops retrying until the config changes.
- `ConfigStore`: Keystore-encrypted `BrokerConfig` (from spa-android).

### App layer

- `CoolerController`: folds incoming messages into `CoolerState`,
  `TrendBuffer`, `PendingCommands`; issues commands.
- `CoolerViewModel`: `StateFlow<UiState>`; a 1 s ticker drives "updated Ns
  ago", `Health` and pending expiry.
- Lifecycle: connect when the app reaches STARTED, disconnect at STOPPED. No
  background service, no WorkManager.

## 8. Error handling

| Situation | Behaviour |
|---|---|
| Bad credentials | `Login refused`; no retry until config changes |
| TLS / certificate failure | `Can't verify the broker (<reason>)`; never falls back to plain |
| Offline / broker unreachable | `Broker unreachable`, backoff retry; last data kept with its age; controls disabled |
| Controller offline | `Controller offline`; controls disabled; commands not queued |
| Online, no `/data` 5 min | `Controller silent` warning banner |
| Bad JSON / wrong `v` | ignored, logged; last good state kept |
| No confirmation in 3 s | revert + "Not applied" |
| Controller clamps | show the controller's value |
| App backgrounded | disconnect; reconnect on return |

## 9. Testing

- **JVM unit tests** for every `model/` unit:
  parser (full payload, nulls, partial, wrong `v`, not JSON);
  `StatusText` — every case in CoolerPanel `tests/test_status_text.cpp`
  ported one-for-one; `Bounds` (every edge + dynamic `fin_recover` floor);
  `Commands` JSON; `PendingCommands` (confirm, clamp-confirm, expiry);
  `Health` (each link state, the 5-min boundary, banner set);
  `TrendBuffer` (seed dating, de-dup, cap, `samples(since)`);
  `BrokerConfig` validation.
- **`HiveMqSessionTest` / repository tests** with a fake transport: subscribe
  set, backoff sequence, auth-failure stop.
- **Compose UI tests**: Status renders a fixed state (wording, chips,
  banners); a Settings stepper goes pending → confirmed, and pending → reverted.
- **Bench check** against the live controller: fills from retained data on
  launch; a setpoint change shows on app and panel; calibrate start/abort
  round-trips; unplugging the controller shows `Controller offline`.

## 10. Out of scope

Push notifications, long-term history (both Node-RED later), remote override,
multiple users / view-only logins, home-screen widgets, iOS, Play Store.
