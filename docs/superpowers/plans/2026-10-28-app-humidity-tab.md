# CoolerApp Humidity Tab + Typed Entry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Humidity tab in CoolerApp (Graph + Controls pages for the dehumidifier), typed entry on every number setting (both settings pages), and a Node-RED `dehum/history` flow.

**Architecture:** A second controller, `DehumController`, shares the app's one MQTT transport and owns its own UI state, health and trend; the Cooler's `CoolerController` is untouched except for typed entry. Existing trend pieces (`TrendBuffer`, `TrendAverage`, `TrendSegments`, `TrendChart`) are generalised from "temperature" to "the primary value" so the humidity graph reuses them. Node-RED gets a second function on the `history.js` pattern.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3), HiveMQ MQTT client, kotlinx.serialization, JUnit 4 + kotlinx-coroutines-test; Node-RED function + `node --test`.

**Spec:** `docs/superpowers/specs/2026-10-28-app-humidity-tab-design.md` (dehumidifier topics: `RULES-dehum.md`).

## Global Constraints

- No firmware changes (dehumidifier or Cooler). No CoolerPanel changes.
- Dehumidifier topics fixed: `dehum/data`, `dehum/availability`, `dehum/cmd`, `dehum/history` — not built from the app's topic base.
- `dehum/cmd` payloads: one JSON object, integer values only, `mode` a lowercase string from `auto on off timer cycle`; never retained.
- Ranges (spec §4): `timer_h` 1..24 step 1; `cycle_on` max(1, `min_run`)..240 step 5; `cycle_off` 1..240 step 5; `min_run` 0..60 step 1; `dehum_rh` 30..95 step 1.
- Pending: confirmed only by a live `dehum/data` received after sending that reports the value; reverted with a "Not applied" message after 3 s (`PendingCommands.TIMEOUT_MS`).
- Dehumidifier offline when `dehum/availability` = `offline`, or no live `dehum/data` for 90 s (from the later of the last live data and connecting).
- The Cooler tabs must behave exactly as now when the dehumidifier and its topics are absent.
- A typed value outside the row's live range can never be sent.
- Broker and Node-RED changes on champion only with the user's explicit go-ahead; edit only cooler/dehum ACL blocks; back up first.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Work on branch `dehumidifier`.
- Unit tests: `cd CoolerApp && ./gradlew testDebugUnitTest`. No phone/emulator attached: UI tests must at least compile (`./gradlew compileDebugAndroidTestKotlin`); run `connectedDebugAndroidTest` when a device is attached.

## Review Focus

1. `dehum/data` with `humidity: null` (sensor dead) — readout "--", average "-.-", graph gap; no crash. (Task 2 parser test, Task 4 controller test.)
2. Typed text like `" 45 "`, `"+5"`, `"045"`, `"4.5"`, `""`, `"-"`, `"99999999999"` — whole-number parse after trim, overflow rejected, never a crash. (Task 1 `TypedEntryTest`.)
3. Cooler offline while the dehumidifier is online (the remote Cooler is often unreachable) — Humidity controls stay enabled; and the reverse. (Task 4 test `dehum health is independent of the cooler`.)
4. A retained `dehum/data` hours old on connect with no `availability` — shown, but treated as silent after 90 s from connect, controls disabled. (Task 4 test.)
5. Refused optional subscription (`dehum/data`) — app stays connected, Humidity tab shows "No dehumidifier access", Cooler tabs unaffected. (Task 3 HiveMqSession test.)

---

### Task 1: Typed entry (shared field + Cooler Settings)

**Files:**
- Create: `CoolerApp/app/src/main/java/ai/northtrail/cooler/model/TypedEntry.kt`
- Create: `CoolerApp/app/src/main/java/ai/northtrail/cooler/ui/NumberField.kt`
- Modify: `CoolerApp/app/src/main/java/ai/northtrail/cooler/CoolerController.kt` (add `enterSetting`)
- Modify: `CoolerApp/app/src/main/java/ai/northtrail/cooler/CoolerViewModel.kt`, `ui/SettingsScreen.kt`, `ui/CoolerApp.kt`
- Test: `CoolerApp/app/src/test/java/ai/northtrail/cooler/model/TypedEntryTest.kt`, `CoolerControllerTest.kt`, `androidTest/.../ui/SettingsScreenTest.kt`

**Interfaces:**
- Produces: `object TypedEntry { fun parse(text: String, lo: Int, hi: Int): Int?; fun hint(lo: Int, hi: Int): String }` — `hint` returns `"$lo–$hi"` (en dash).
- Produces: `@Composable fun SteppedValue(label: String, valueText: String, pending: Boolean, lo: Int, hi: Int, enabled: Boolean, open: Boolean, onOpen: () -> Unit, onClose: () -> Unit, onSend: (Int) -> Unit, onStep: (Int) -> Unit, tag: String)` in `ui/NumberField.kt`: the row's value + −/+ buttons; when `open`, a numeric `OutlinedTextField` (keyboard `KeyboardType.Number`, `ImeAction.Send`) + **Send** button + hint text replace the value. Test tags: `value-$tag`, `field-$tag`, `send-$tag`, `pending-$tag`.
- Produces: `CoolerController.enterSetting(key: String, value: Int): Boolean` — false (sends nothing) when controls are disabled, the key is unknown or a preset, or `value` is outside `Bounds.floor(key, finCutoff)..hi`; otherwise goes through the existing `edit()` debounce/pending path; true.

- [ ] **Step 1: Write the failing tests**

`TypedEntryTest`:
```kotlin
@Test fun parsesWholeNumbersInRange() {
    assertEquals(45, TypedEntry.parse(" 45 ", 1, 240))
    assertEquals(45, TypedEntry.parse("045", 1, 240))
    assertEquals(-3, TypedEntry.parse("-3", -5, 5))
    assertEquals(1, TypedEntry.parse("1", 1, 240)); assertEquals(240, TypedEntry.parse("240", 1, 240))
}
@Test fun rejectsEverythingElse() {
    listOf("", " ", "-", "+5", "4.5", "4,5", "abc", "0", "241", "99999999999", "-99999999999")
        .forEach { assertNull(it, TypedEntry.parse(it, 1, 240)) }
}
@Test fun hintShowsRange() { assertEquals("1–240", TypedEntry.hint(1, 240)); assertEquals("-5–5", TypedEntry.hint(-5, 5)) }
```
`CoolerControllerTest` (existing harness, payload `finCutoff = 1`):
- `enterSetting sends a typed value off the step grid`: online, `enterSetting("minruntime", 45)` → true; after `DEBOUNCE_MS` `published.last().payload == """{"minruntime":45}"""`; `isPending("minruntime")`.
- `enterSetting refuses out of range and live floor`: `enterSetting("minruntime", 601)`, `enterSetting("fin_recover", 1)` (floor is cutoff+1 = 2) → false, nothing published.
- `enterSetting refuses presets and offline`: `enterSetting("sampleinterval", 60)` → false; with no state (`controlsEnabled` false) `enterSetting("coolerset", 5)` → false.

`SettingsScreenTest` (androidTest): tapping `value-coolerset` shows `field-coolerset`; typing `41` leaves `send-coolerset` disabled and shows `2–40`; typing `7` and clicking Send calls `onEnter("coolerset", 7)` and closes the field; opening `value-maxrun` closes the coolerset field.

- [ ] **Step 2: Run and see them fail**

Run: `cd CoolerApp && ./gradlew testDebugUnitTest --tests '*TypedEntryTest' --tests '*CoolerControllerTest'`
Expected: compilation FAIL — `TypedEntry`, `enterSetting` unresolved.

- [ ] **Step 3: Implement** `TypedEntry` (trim, `toIntOrNull()`, reject a leading `+`, range check), `enterSetting`, `SteppedValue`; replace `SettingsScreen`'s `StepperRow` internals with `SteppedValue`. `SettingsScreen` gains `onEnter: (String, Int) -> Unit` and holds `var openKey by rememberSaveable { mutableStateOf<String?>(null) }` (one field open at a time; Back via `BackHandler(open)` closes it). Preset rows unchanged. `CoolerViewModel.enterSetting` forwards; `CoolerApp` passes `viewModel::enterSetting`.

- [ ] **Step 4: Run and see them pass**

Run: `./gradlew testDebugUnitTest && ./gradlew compileDebugAndroidTestKotlin`
Expected: BUILD SUCCESSFUL, all unit tests pass.

- [ ] **Step 5: Commit** — `feat(app): typed entry for Cooler settings`

---

### Task 2: Dehumidifier model (topics, state, commands, bounds, history) + trend generalisation

**Files:**
- Create: `model/DehumTopics.kt`, `model/DehumState.kt` (state + parser), `model/DehumCommands.kt`, `model/DehumBounds.kt`, `model/DehumHistoryParser.kt`
- Modify: `model/TrendBuffer.kt`, `model/TrendAverage.kt`, `ui/TrendChart.kt` (rename `Sample.tempC` → `Sample.value`, KDoc: "the primary series: °C for the Cooler, %RH for the dehumidifier"); `model/PendingCommands.kt` (generalise)
- Test: `model/DehumStateParserTest.kt`, `model/DehumCommandsTest.kt`, `model/DehumBoundsTest.kt`, `model/DehumHistoryParserTest.kt`, `model/PendingCommandsTest.kt`

**Interfaces:**
- Produces: `object DehumTopics { const val DATA = "dehum/data"; const val AVAILABILITY = "dehum/availability"; const val CMD = "dehum/cmd"; const val HISTORY = "dehum/history"; val subscriptions = listOf(DATA, AVAILABILITY, HISTORY) }`
- Produces: `enum class DehumMode(val wire: String, val label: String) { AUTO("auto","AUTO"), ON("on","ON"), OFF("off","OFF"), TIMER("timer","TIMER"), CYCLE("cycle","CYCLE"); companion object { fun of(wire: String?): DehumMode? } }` — button order in the UI: ON, OFF, TIMER, AUTO, CYCLE.
- Produces: `data class DehumState(val mode: DehumMode?, val relay: Boolean, val want: Int?, val linkLost: Boolean, val holdS: Long, val remainS: Long, val phaseOn: Boolean?, val temp: Double?, val humidity: Double?, val humid: Boolean, val settings: Map<String, Int>, val runS: Long, val uptimeS: Long?)` and `object DehumStateParser { const val VERSION = 1; fun parse(payload: String): DehumState? }` — null unless a JSON object with `v == 1`; `settings` holds whichever of `DehumBounds.KEYS` are integers; `phase` `"on"`→true, `"off"`→false, else null; `want` 0/1 else null.
- Produces: `object DehumBounds { val KEYS = listOf("timer_h","cycle_on","cycle_off","min_run","dehum_rh"); val ALL: List<Bound>; fun floor(key: String, minRun: Int): Int; fun hi(key: String): Int; fun format(key: String, v: Int?): String }` — reuses `Bound` with `Widget.STEPPER` and a new `Group.DEHUM("Dehumidifier")`; labels "Timer" h, "Cycle run" min, "Cycle rest" min, "Min run" min, "AUTO above" %RH; `floor("cycle_on", minRun) = max(1, minRun)`.
- Produces: `class DehumCommands { fun mode(m: DehumMode): Publish; fun setting(key: String, value: Int): Publish }` → topic `dehum/cmd`, payloads `{"mode":"timer"}`, `{"cycle_on":45}`; `setting` requires a key in `DehumBounds.KEYS`.
- Produces: `object DehumHistoryParser { fun parse(payload: String, nowS: Long): HistoryWindow? }` — same validation as `HistoryParser` (v 1, interval 60, t0 valid, ≤1440 slots, equal lengths of `hum`, `temp`, `relay`, not > 120 s in the future); a slot is kept when `hum` is a number, as `Sample(epochS, value = hum, relay, humidity = null)`.
- Produces: `PendingCommands.resolve(pending: Map<String, Pending>, reported: Map<String, Int>?, stateLiveAtMillis: Long?, nowMillis: Long): Resolution` — `CoolerController` passes `next.state?.settings`.

- [ ] **Step 1: Write the failing tests**
- `DehumStateParserTest`: the firmware's real payload `{"v":1,"mode":"cycle","relay":1,"want":null,"link_lost":0,"hold_s":120,"remain_s":300,"phase":"on","temp":23.6,"humidity":54.5,"humid":0,"dehum_rh":65,"min_run":10,"timer_h":2,"cycle_on":30,"cycle_off":30,"run_s":25,"uptime_s":20}` → mode CYCLE, relay true, want null, phaseOn true, humidity 54.5, settings `{timer_h=2, cycle_on=30, cycle_off=30, min_run=10, dehum_rh=65}`; `"humidity":null,"temp":null` → both null; `v:2`, `[]`, `not json` → null; `"mode":"bogus"` → mode null.
- `DehumCommandsTest`: `mode(TIMER)` → `Publish("dehum/cmd", """{"mode":"timer"}""")`; `setting("cycle_off", 45)` → `{"cycle_off":45}`; `setting("coolerset", 4)` throws.
- `DehumBoundsTest`: `floor("cycle_on", 10) == 10`, `floor("cycle_on", 0) == 1`, `floor("min_run", 10) == 0`, `hi("dehum_rh") == 95`, steps `cycle_on`/`cycle_off` 5, others 1; `format("dehum_rh", 65) == "65 %RH"`, `format("timer_h", null) == "--"`.
- `DehumHistoryParserTest`: a 3-slot window with `hum [55, null, 57]`, `relay [1, 0, null]` → 2 samples, values 55.0 and 57.0, relays true and null; mismatched lengths, `v:2`, 1441 slots, window ending 10 min in the future → null.
- `PendingCommandsTest`: existing tests updated to pass `state.settings`; plus one passing a plain map `mapOf("min_run" to 15)`.

- [ ] **Step 2: Run** `./gradlew testDebugUnitTest` — Expected: compilation FAIL (new types unresolved).
- [ ] **Step 3: Implement** the files above; do the `tempC` → `value` rename (7 uses: `TrendBuffer`, `TrendAverage`, `TrendChart`) and update `CoolerController`'s `resolve` call.
- [ ] **Step 4: Run** `./gradlew testDebugUnitTest` — Expected: all pass (old trend tests unchanged in behaviour).
- [ ] **Step 5: Commit** — `feat(app): dehumidifier model and parsers`

---

### Task 3: Subscribe to dehumidifier topics; report refused optional topics

**Files:**
- Modify: `model/Topics.kt` (`optionalSubscriptions = listOf(history) + DehumTopics.subscriptions`), `model/Health.kt` (`LinkStatus` gains `refused: Set<String> = emptySet()`), `data/HiveMqSession.kt`, `data/MqttRepository.kt`
- Test: `model/TopicsTest.kt`, `data/HiveMqSessionTest.kt`

**Interfaces:**
- Produces: `LinkStatus.refused` — optional topic filters the broker refused in the SUBACK of the current session (empty otherwise). `HiveMqSession`'s `onLink` callback gains a `refused: Set<String>` argument (default empty for non-CONNECTED states); `MqttRepository.setLinkFrom` copies it into `LinkStatus`.

- [ ] **Step 1: Failing tests** — `TopicsTest`: `Topics("cooler").optionalSubscriptions == listOf("cooler/history","dehum/data","dehum/availability","dehum/history")` and `subscriptions` unchanged. `HiveMqSessionTest` with `FakeBroker(refuse = setOf("dehum/data"))`: link ends CONNECTED (not REJECTED) and the reported refused set is `setOf("dehum/data")`; with no refusals the set is empty.
- [ ] **Step 2: Run** `./gradlew testDebugUnitTest --tests '*TopicsTest' --tests '*HiveMqSessionTest'` — Expected: FAIL.
- [ ] **Step 3: Implement** — refused = optional filters whose SUBACK return code (index `subscriptions.size + i`) is FAILURE.
- [ ] **Step 4: Run** full `./gradlew testDebugUnitTest` — Expected: pass (existing `FatalReasonTest`, `ConnectGateTest` unaffected).
- [ ] **Step 5: Commit** — `feat(app): subscribe to dehumidifier topics as optional`

Note: `BrokerProbe` (Setup's "Test & save") is not changed: it checks Cooler access only.

---

### Task 4: DehumController

**Files:**
- Create: `CoolerApp/app/src/main/java/ai/northtrail/cooler/DehumController.kt`, `model/DehumHealth.kt`
- Modify: `CoolerViewModel.kt` (construct and start it with the same transport/scope/clock; expose `dehum: StateFlow<DehumUi>` and forwarders)
- Test: `CoolerApp/app/src/test/java/ai/northtrail/cooler/DehumControllerTest.kt`, `model/DehumHealthTest.kt`

**Interfaces:**
- Consumes: Task 2 types, `LinkStatus.refused` (Task 3), `TrendBuffer`, `PendingCommands`.
- Produces: `enum class DehumView { NO_ACCESS, BROKER_DOWN, WAITING, OFFLINE, SILENT, ONLINE }` and `object DehumHealth { const val SILENT_AFTER_MS = 90_000L; fun evaluate(link: LinkStatus, online: Boolean?, state: DehumState?, lastLiveDataAtMillis: Long?, nowMillis: Long): DehumView }` — NO_ACCESS when `"dehum/data" in link.refused`; BROKER_DOWN when the link is not CONNECTED; OFFLINE when `online == false`; WAITING when `state == null`; SILENT when `now − max(lastLive, connectedAt) > 90 s`; else ONLINE. Text: "No dehumidifier access (check the broker ACL)", "Broker unreachable", "Waiting for dehumidifier", "Dehumidifier offline", "Dehumidifier silent: no data for 90 s", "Online".
- Produces: `data class DehumUi(val view: DehumView, val state: DehumState?, val stateLiveAtMillis: Long?, val lastLiveDataAtMillis: Long?, val online: Boolean?, val trend: TrendBuffer, val drafts: Map<String, Int>, val pending: Map<String, Pending>, val message: String?) { val controlsEnabled: Boolean /* view == ONLINE */; fun settingValue(key: String): Int?; fun isPending(key: String): Boolean }`
- Produces: `class DehumController(transport: CoolerTransport, scope: CoroutineScope, clock: () -> Long, onIgnored: (String) -> Unit = {})` with `start()`, `setMode(m: DehumMode)`, `stepSetting(key: String, direction: Int)`, `enterSetting(key: String, value: Int): Boolean`, `consumeMessage()`, `ui: StateFlow<DehumUi>`. Behaviour mirrors `CoolerController`: 400 ms debounce, pending/timeout (message "Not applied: <label>"), "Couldn't send: dehumidifier not reachable" when publish fails or controls are disabled, a 1 s ticker, new session clears live timestamps. Live (non-retained) `dehum/data` with a number `humidity` appends `Sample(nowS, value = humidity, relay)`; `dehum/history` merges via `withHistory`. `setMode` publishes at once (no debounce, no pending). `enterSetting` bounds: `DehumBounds.floor(key, settingValue("min_run") ?: 0)..hi(key)`.
- Reused message: the snackbar shows whichever of the two controllers' `message` is non-null.

- [ ] **Step 1: Failing tests** (`DehumControllerTest`, a `FakeTransport` like `CoolerControllerTest`'s, link CONNECTED):
  - `live data goes online and into the trend`: emit the Task 2 payload → view ONLINE, `state.mode == CYCLE`, trend has 1 sample of 54.5.
  - `humidity null is a gap not a sample`: emit with `"humidity":null` → state set, trend empty.
  - `mode tap publishes at once`: `setMode(TIMER)` → `published.single() == Publish("dehum/cmd","""{"mode":"timer"}""")` without advancing time.
  - `step uses the dehum step and floor`: settings cycle_on 30, min_run 10 → `stepSetting("cycle_on", +1)`, advance 400 ms → `{"cycle_on":35}` published and pending; a live data with cycle_on 35 confirms it; `stepSetting` down from cycle_on 10 with min_run 10 publishes nothing.
  - `pending times out`: no confirming data within 3 s → pending cleared, message `Not applied: Cycle run`.
  - `enterSetting checks the live range`: `enterSetting("cycle_on", 5)` with min_run 10 → false; `enterSetting("dehum_rh", 70)` → true and `{"dehum_rh":70}` after debounce.
  - `dehum health is independent of the cooler`: `cooler/availability` `offline` arriving changes nothing in `DehumUi.view`.
  - `retained data alone goes silent after 90 s`: retained payload at connect → ONLINE; advance 91 s → SILENT, `controlsEnabled` false, `setMode(ON)` publishes nothing and sets the not-reachable message.
  - `offline availability disables controls`: `dehum/availability` `offline` → OFFLINE.
  - `history merges`: a `dehum/history` window adds its samples to the trend.
  - `DehumHealthTest`: refused `dehum/data` → NO_ACCESS even while CONNECTED.
- [ ] **Step 2: Run** `./gradlew testDebugUnitTest --tests '*Dehum*'` — Expected: compilation FAIL.
- [ ] **Step 3: Implement** `DehumHealth`, `DehumController`, view-model wiring.
- [ ] **Step 4: Run** `./gradlew testDebugUnitTest` — Expected: all pass.
- [ ] **Step 5: Commit** — `feat(app): dehumidifier controller`

---

### Task 5: Humidity tab UI

**Files:**
- Create: `ui/HumidityScreen.kt` (two pages), `ui/HumidityGraph.kt`, `ui/HumidityControls.kt`
- Modify: `ui/TrendChart.kt` (generalise), `ui/StatusScreen.kt` (call site), `ui/CoolerApp.kt` (fourth tab), `ui/Theme.kt` (one threshold colour if needed), `CoolerApp/README.md`
- Test: `androidTest/.../ui/HumidityScreenTest.kt`, `model/DehumStatusTextTest.kt` (+ `model/DehumStatusText.kt`)

**Interfaces:**
- Consumes: `DehumUi`, `DehumMode`, `DehumBounds`, `SteppedValue` (Task 1), `TrendAverage`, `TrendWindow`/`TrendZoom`.
- Produces: `TrendChart` gains `unit: String = "°"` (axis and min/max/avg labels) and `threshold: Double? = null` (dashed horizontal line, included in the y range); the Cooler call site passes nothing new. Humidity graph calls it with `shown = SeriesShown(temp = true, rh = false, avg = showAvg)`, `setpoint = null`, `range = null`, `unit = "%"`, `threshold = dehum_rh`.
- Produces: `object DehumStatusText { fun mode(s: DehumState?): String; fun auto(s: DehumState?): String }` — `mode`: "AUTO", "ON", "OFF", "TIMER 1 h 23 m left", "CYCLE run 12 m left", "CYCLE rest 4 m left" (from `remain_s`, minutes rounded up, hours shown when ≥ 60 min), "--" for null; `auto` (AUTO only, else ""): want 1 → "AC resting", 0 → "AC cooling", null → "no Cooler signal", then " · humid" / " · dry".
- Bottom tab: `Tab("Humidity", Icons.Filled.WaterDrop)` inserted after Settings (order Status, Settings, Humidity, Detail). `HumidityScreen(ui: DehumUi, nowEpochS: Long, onMode, onStep, onEnter)` with a `TabRow` "Graph" / "Controls" (`rememberSaveable` page).
- Graph page: strip with the `DehumView` text; readout = humidity (no decimals, "--" if null) and `TrendAverage.text(TrendAverage.latest(ui.trend, now))` in the average colour; status line `DehumStatusText.mode` · relay "Running"/"Stopped" · `auto`, plus "Broker link lost: off" in the warning colour when `state.linkLost`; chart 220 dp with `average = TrendAverage.rolling(...)` and `windowMean = TrendAverage.mean(...)` exactly as `StatusScreen` does; zoom chips 1h/3h/6h/12h (default 3h) and an "Avg" toggle.
- Controls page: offline notice "Controls work only while the dehumidifier is online." when `!controlsEnabled`; mode buttons ON / OFF / TIMER / AUTO / CYCLE (`FilterChip`, selected = `state.mode`, tags `mode-<wire>`); five `SteppedValue` rows from `DehumBounds.ALL`; while mode is TIMER the Timer row's hint adds "Lowering below the time already run ends the timer."

- [ ] **Step 1: Failing tests** — `DehumStatusTextTest`: TIMER with remain_s 4980 → "TIMER 1 h 23 m left"; CYCLE phase on remain 700 → "CYCLE run 12 m left"; phase off remain 240 → "CYCLE rest 4 m left"; AUTO want null humid false → auto "no Cooler signal · dry". `HumidityScreenTest` (androidTest): Controls page shows five mode chips with `mode-cycle` selected for the sample state; clicking `mode-timer` calls `onMode(TIMER)`; with view OFFLINE all chips disabled and the notice shown; Graph page shows "54" and "TIMER …" text.
- [ ] **Step 2: Run** `./gradlew testDebugUnitTest --tests '*DehumStatusTextTest'` — Expected: FAIL.
- [ ] **Step 3: Implement** the screens, `TrendChart` parameters, tab wiring; README: one paragraph on the Humidity tab and the typed entry.
- [ ] **Step 4: Run** `./gradlew testDebugUnitTest && ./gradlew compileDebugAndroidTestKotlin && ./gradlew assembleDebug` — Expected: BUILD SUCCESSFUL.
- [ ] **Step 5: Commit** — `feat(app): Humidity tab with graph and controls`

---

### Task 6: Node-RED `dehum/history`

**Files:**
- Create: `nodered/dehum-history.js`, `nodered/dehum-history.json` (generated), `nodered/test/dehum-history.test.mjs`
- Modify: `nodered/build-flow.mjs` (also emits the dehum flow), `nodered/README.md`

**Interfaces:**
- Produces: a flow tab "Dehumidifier history": `mqtt in` `dehum/data` (auto-detect, `rap: false`) and `dehum/availability` (utf8) → function `dehum history` → `mqtt out` `dehum/history` (QoS 0, not retained). It **reuses the existing broker config node id `c001e70000000002`** so both flows share one login; new node ids `c001e70000000011`…`…15`.
- Function behaviour = `history.js` with: topics `dehum/*`; accepts `v === 1`; arrays `hum` (`humidity`, 0 decimals), `temp` (1 decimal), `relay`; flow keys `dehum_hist`, `dehum_avail`.

- [ ] **Step 1: Failing tests** (`dehum-history.test.mjs`, same harness as `history.test.mjs`): exported func equals `dehum-history.js`; subscribes exactly `dehum/data` + `dehum/availability` with `rap === false`; out topic `dehum/history`, retain false; one live message → `{v:1, t0:T, interval_s:60, hum:[55], temp:[23.6], relay:[1]}`; `humidity:null` → `hum:[null]`; retained message → null; `v:2` → null; while `dehum_avail` is `offline` → null; two minutes apart → arrays length 2 (gap filled with null); 1441 minutes → 1440 slots and t0 advanced.
- [ ] **Step 2: Run** `node --test nodered/test/` — Expected: FAIL (file missing).
- [ ] **Step 3: Implement**, then `node nodered/build-flow.mjs`.
- [ ] **Step 4: Run** `node --test nodered/test/` — Expected: all pass, including the existing cooler tests.
- [ ] **Step 5: README** — install steps for the second flow and its ACL lines (spec §7). **Commit** — `feat(nodered): dehumidifier 24 h history`

---

### Task 7: Broker, Node-RED deploy, phone check (each step needs the user's go-ahead)

- [ ] **Step 1 (read only):** `ssh champion` — show the cooler/dehum ACL blocks; confirm `coolerapp` has read `dehum/data`, `dehum/availability`, write `dehum/cmd`; find Node-RED's login block.
- [ ] **Step 2 (go-ahead):** back up `/etc/mosquitto/acl`; add Node-RED: read `dehum/data`, read `dehum/availability`, write `dehum/history`; `coolerapp` and `coolerpanel`: read `dehum/history`. Reload Mosquitto; test with `mosquitto_sub` as `coolerapp` that `dehum/data` arrives.
- [ ] **Step 3 (user, in Node-RED):** import `nodered/dehum-history.json`, keep the shared broker node, Deploy. Expected: `dehum/history` appears within 30 s.
- [ ] **Step 4 (phone attached):** `./gradlew installDebug`; check the Humidity tab: live readout, typed entry on both settings pages, a mode change on the bench dehumidifier (relay not on mains), Cooler tabs unchanged. Run `./gradlew connectedDebugAndroidTest` while the phone is attached.
- [ ] **Step 5:** update `RULES-dehum.md` (app section, `dehum/history`) and the memory note; commit `docs: dehumidifier app and history`.
