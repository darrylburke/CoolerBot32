# 24-Hour History from Node-RED Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Node-RED keeps the last 24 h of box temperature, humidity and relay state and publishes it on `cooler/history` after every live `cooler/data`; the app and panel merge it into their trends.

**Architecture:** One Node-RED Function node holds a 1,440-slot window in flow memory and emits the whole window (not retained) on each live `cooler/data`. CoolerApp subscribes to `cooler/history` as an optional subscription, parses it into trend samples and replaces its own samples inside the covered span. CoolerPanel does the same into its PSRAM history ring, but only when the message covers minutes the panel lacks.

**Tech Stack:** Node-RED Function node (JavaScript), Node 21 `node:test`; Kotlin, kotlinx.serialization, JUnit 4, HiveMQ MQTT client; C++17, ArduinoJson 7, doctest, PubSubClient, libmosquitto (simulator).

**Spec:** `docs/superpowers/specs/2026-10-03-nodered-history-design.md`

## Global Constraints

- Topic `cooler/history`, published QoS 0, **retain false**, by Node-RED only.
- Payload `{"v":1,"t0":<epoch s, multiple of 60>,"interval_s":60,"temp":[...],"hum":[...],"relay":[...]}`; arrays of equal length 1..1440; `temp` one decimal, `hum` whole number, `relay` 0/1; `null` for no reading.
- Consumers reject: `v != 1`, `interval_s != 60`, `t0 < 1600000000`, unequal or empty arrays or more than 1440, `t0 + n*60 > now + 120` (this last check skipped when `now < 1600000000`).
- Node-RED subscribes to `cooler/data` and `cooler/availability` only (never `cooler/#`), with "retain as published" on, and ignores retained replays and anything while availability is `offline`.
- No credentials in any committed file. The Redis nodes from the user's draft flow are not used.
- The controller firmware is not changed.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never `git add` the untracked PDF in the repo root.

## Review Focus

1. **Broker ACL not yet updated for `cooler/history`** — the app must still connect and work; a refusal of the history subscription is not fatal. Test: Task 3, `refusedOptionalSubscriptionStillConnects`.
2. **Phone or panel clock not yet set** (epoch near 0) — a valid history message must still be accepted rather than rejected as "from the future". Tests: Task 2 `futureCheckSkippedWhileOwnClockUnset`, Task 4 `"future check is skipped while the panel clock is unset"`.
3. **Controller offline for more than 24 h, then back** — Node-RED starts a fresh window instead of looping through thousands of null slots. Test: Task 1 `an outage longer than the window starts a fresh one`.
4. **Something other than a cooler/data v2 object arrives** (a string, `{"v":1}`, a `cooler/history` message if someone wires `cooler/#`) — Node-RED outputs nothing and keeps its window. Test: Task 1 `ignores payloads that are not cooler/data v2` and `ignores other topics`.
5. **The same history message arrives twice a minute on a panel that has been connected all along** — the panel must not rebuild its 20,160-entry ring each time. Test: Task 5 `"a /history message fills the minutes the panel never saw"` (second delivery changes nothing) and Task 4 coverage tests.

---

## File structure

| File | Responsibility |
|---|---|
| `nodered/history.js` (create) | The Function node's code: window update and payload. Source of truth. |
| `nodered/build-flow.mjs` (create) | Writes `nodered/cooler-history.json` from the node layout plus `history.js`. |
| `nodered/cooler-history.json` (generated, committed) | Import file for Node-RED. |
| `nodered/test/history.test.mjs` (create) | Runs the Function code from the JSON with a fake flow context and clock. |
| `nodered/README.md` (create) | Import and broker setup. |
| `README.md` (modify) | Topic table, ACL table, diagram, companion list, status. |
| `CoolerApp/.../model/HistoryParser.kt` (create) | `cooler/history` payload → `HistoryWindow`. |
| `CoolerApp/.../model/TrendBuffer.kt` (modify) | `withHistory(HistoryWindow)`. |
| `CoolerApp/.../model/Topics.kt` (modify) | `history`, `optionalSubscriptions`. |
| `CoolerApp/.../data/HiveMqSession.kt` (modify) | Subscribe optional topics; ignore their refusal. |
| `CoolerApp/.../data/MqttRepository.kt` (modify) | Pass optional subscriptions. |
| `CoolerApp/.../CoolerController.kt` (modify) | Route `history` into the trend. |
| `CoolerPanel/shared/model/history_msg.{h,cpp}` (create) | Parse the payload; decide whether it adds coverage. |
| `CoolerPanel/shared/model/history.{h,cpp}` (modify) | `merge_window`. |
| `CoolerPanel/shared/model/mqtt_router.{h,cpp}` (modify) | `router_is_leaf`. |
| `CoolerPanel/shared/app.cpp` (modify) | Route `<base>/history` into the ring. |
| `CoolerPanel/device/src/mqtt_pubsub.cpp`, `CoolerPanel/sim/mqtt_mosq.cpp` (modify) | Subscribe; device buffer 24 KB. |

App paths below abbreviate `CoolerApp/app/src/main/java/ai/northtrail/cooler` as `APP` and `CoolerApp/app/src/test/java/ai/northtrail/cooler` as `APPTEST`.

---

### Task 1: Node-RED flow

**Files:**
- Create: `nodered/history.js`, `nodered/build-flow.mjs`, `nodered/cooler-history.json` (generated), `nodered/test/history.test.mjs`, `nodered/README.md`
- Modify: `README.md`

**Interfaces:**
- Produces: the `cooler/history` payload (Global Constraints) consumed by Tasks 2–5.

- [ ] **Step 1: Write the failing test** — `nodered/test/history.test.mjs`

```js
// Runs the "history" Function node exactly as exported in cooler-history.json,
// with a fake flow context and clock. Run: node --test nodered/test/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const here = new URL('..', import.meta.url);
const nodes = JSON.parse(readFileSync(new URL('cooler-history.json', here), 'utf8'));
const fnNode = nodes.find((n) => n.type === 'function' && n.name === 'history');
const T = 1_790_700_000; // a minute boundary

function harness() {
  const store = new Map();
  const flow = { get: (k) => store.get(k), set: (k, v) => store.set(k, v) };
  let nowMs = T * 1000;
  const FakeDate = { now: () => nowMs };
  const run = new Function('msg', 'flow', 'node', 'Date', fnNode.func);
  return {
    at(s) { nowMs = s * 1000; },
    data(payload, { retain = false, topic = 'cooler/data' } = {}) {
      const out = run({ topic, payload, retain }, flow, {}, FakeDate);
      return out ? { ...out, body: JSON.parse(out.payload) } : null;
    },
    avail(text) { return run({ topic: 'cooler/availability', payload: text }, flow, {}, FakeDate); },
  };
}
const d = (temp, humidity = 80, relay = 0) => ({ v: 2, temp, humidity, relay });

test('the exported function is history.js verbatim', () => {
  assert.equal(fnNode.func, readFileSync(new URL('history.js', here), 'utf8'));
});

test('subscribes to exactly data and availability, retain as published', () => {
  const ins = nodes.filter((n) => n.type === 'mqtt in');
  assert.deepEqual(ins.map((n) => n.topic).sort(), ['cooler/availability', 'cooler/data']);
  assert.ok(ins.every((n) => n.rap === true));
  const out = nodes.find((n) => n.type === 'mqtt out');
  assert.equal(out.topic, 'cooler/history');
  assert.equal(out.retain, 'false');
});

test('first message starts the window at its minute', () => {
  const h = harness();
  h.at(T + 59);
  const out = h.data(d(4.2, 80, 0));
  assert.equal(out.topic, 'cooler/history');
  assert.equal(out.retain, false);
  assert.deepEqual(out.body, { v: 1, t0: T, interval_s: 60, temp: [4.2], hum: [80], relay: [0] });
});

test('a later message in the same minute overwrites the slot', () => {
  const h = harness();
  h.data(d(4.2, 80, 0));
  h.at(T + 50);
  const out = h.data(d(4.4, 81, 1));
  assert.deepEqual([out.body.temp, out.body.hum, out.body.relay], [[4.4], [81], [1]]);
});

test('missing minutes become null slots', () => {
  const h = harness();
  h.data(d(4.0));
  h.at(T + 180);
  const out = h.data(d(4.5, 82, 1));
  assert.deepEqual(out.body.temp, [4.0, null, null, 4.5]);
  assert.deepEqual(out.body.hum, [80, null, null, 82]);
  assert.deepEqual(out.body.relay, [0, null, null, 1]);
});

test('keeps 1440 slots and drops the oldest', () => {
  const h = harness();
  let out;
  for (let i = 0; i <= 1440; i++) { h.at(T + i * 60); out = h.data(d(4.0)); }
  assert.equal(out.body.temp.length, 1440);
  assert.equal(out.body.t0, T + 60);
});

test('a retained replay is ignored', () => {
  const h = harness();
  assert.equal(h.data(d(9.9), { retain: true }), null);
  assert.deepEqual(h.data(d(4.0)).body.temp, [4.0]);
});

test('nothing is recorded while the controller is offline', () => {
  const h = harness();
  h.avail('offline');
  assert.equal(h.data(d(4.0)), null);
  h.avail('online');
  assert.deepEqual(h.data(d(4.1)).body.temp, [4.1]);
});

test('an SHT30 fault keeps the relay but nulls temp and humidity', () => {
  const h = harness();
  const out = h.data({ v: 2, temp: null, humidity: null, relay: 1 });
  assert.deepEqual([out.body.temp, out.body.hum, out.body.relay], [[null], [null], [1]]);
});

test('rounds temp to 0.1 and humidity to 1', () => {
  const h = harness();
  const out = h.data(d(4.26, 79.6, 0));
  assert.deepEqual([out.body.temp, out.body.hum], [[4.3], [80]]);
});

test('a clock going backwards starts a fresh window', () => {
  const h = harness();
  h.at(T + 600);
  h.data(d(5.0));
  h.at(T);
  const out = h.data(d(4.0));
  assert.deepEqual([out.body.t0, out.body.temp], [T, [4.0]]);
});

test('an outage longer than the window starts a fresh one', () => {
  const h = harness();
  h.data(d(4.0));
  h.at(T + 3 * 86_400);
  const out = h.data(d(4.1));
  assert.deepEqual([out.body.t0, out.body.temp], [T + 3 * 86_400, [4.1]]);
});

test('ignores payloads that are not cooler/data v2', () => {
  const h = harness();
  assert.equal(h.data('not json'), null);
  assert.equal(h.data({ v: 1, temp: 4 }), null);
  assert.equal(h.data(null), null);
  assert.deepEqual(h.data(d(4.0)).body.temp, [4.0]);
});

test('ignores other topics', () => {
  const h = harness();
  assert.equal(h.data(d(4.0), { topic: 'cooler/history' }), null);
  assert.deepEqual(h.data(d(4.1)).body.temp, [4.1]);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node --test nodered/test/`
Expected: FAIL with `ENOENT ... cooler-history.json`.

- [ ] **Step 3: Write `nodered/history.js`**

```js
// Keeps the last 24 h of cooler/data in one-minute slots (flow memory) and
// emits the whole window for cooler/history after every live message.
const STEP = 60, SLOTS = 1440;

if (msg.topic === 'cooler/availability') {
    flow.set('avail', String(msg.payload).trim());
    return null;
}
if (msg.topic !== 'cooler/data') return null;
// A retained copy is the broker replaying its last message when Node-RED
// (re)connects; it may be hours old.
if (msg.retain || flow.get('avail') === 'offline') return null;
const d = msg.payload;
if (!d || typeof d !== 'object' || d.v !== 2) return null;

const num = (x, places) => {
    if (typeof x !== 'number' || !isFinite(x)) return null;
    const f = 10 ** places;
    return Math.round(x * f) / f;
};
const slot = Math.floor(Date.now() / 1000 / STEP) * STEP;
const fresh = () => ({ t0: slot, temp: [], hum: [], relay: [] });

let h = flow.get('hist') || fresh();
let newest = h.t0 + (h.temp.length - 1) * STEP;   // t0 - STEP when empty
// Clock went backwards, or the gap is longer than the window: start again.
if (newest > slot || slot - newest > SLOTS * STEP) { h = fresh(); newest = slot - STEP; }
for (; newest < slot; newest += STEP) { h.temp.push(null); h.hum.push(null); h.relay.push(null); }

const i = h.temp.length - 1;
h.temp[i] = num(d.temp, 1);
h.hum[i] = num(d.humidity, 0);
h.relay[i] = d.relay === 1 ? 1 : d.relay === 0 ? 0 : null;

const drop = h.temp.length - SLOTS;
if (drop > 0) {
    h.temp.splice(0, drop); h.hum.splice(0, drop); h.relay.splice(0, drop);
    h.t0 += drop * STEP;
}
flow.set('hist', h);

msg.topic = 'cooler/history';
msg.retain = false;
msg.payload = JSON.stringify({ v: 1, t0: h.t0, interval_s: STEP, temp: h.temp, hum: h.hum, relay: h.relay });
return msg;
```

- [ ] **Step 4: Write `nodered/build-flow.mjs`**

```js
// Regenerates cooler-history.json from history.js. Run: node nodered/build-flow.mjs
// The broker node carries no credentials: set the login after importing.
import { readFileSync, writeFileSync } from 'node:fs';

const here = new URL('.', import.meta.url);
const func = readFileSync(new URL('history.js', here), 'utf8');
const tab = 'c001e70000000001', broker = 'c001e70000000002', fn = 'c001e70000000003', out = 'c001e70000000004';

const mqttIn = (id, topic, datatype, y) => ({
  id, type: 'mqtt in', z: tab, name: topic, topic, qos: '1', datatype, broker,
  nl: false, rap: true, rh: 0, inputs: 0, x: 170, y, wires: [[fn]],
});

const nodes = [
  { id: tab, type: 'tab', label: 'Cooler history', disabled: false,
    info: '24 h of cooler/data in one-minute slots, republished on cooler/history (not retained). See nodered/README.md.' },
  mqttIn('c001e70000000005', 'cooler/data', 'auto-detect', 100),
  mqttIn('c001e70000000006', 'cooler/availability', 'utf8', 160),
  { id: fn, type: 'function', z: tab, name: 'history', func, outputs: 1, timeout: 0, noerr: 0,
    initialize: '', finalize: '', libs: [], x: 420, y: 130, wires: [[out]] },
  { id: out, type: 'mqtt out', z: tab, name: 'cooler/history', topic: 'cooler/history', qos: '0',
    retain: 'false', respTopic: '', contentType: '', userProps: '', correl: '', expiry: '',
    broker, x: 640, y: 130, wires: [] },
  { id: broker, type: 'mqtt-broker', name: 'Cooler broker (local)', broker: 'localhost', port: '1883',
    clientid: 'node-red-cooler-history', autoConnect: true, usetls: false, protocolVersion: '4',
    keepalive: '60', cleansession: true, autoUnsubscribe: true,
    birthTopic: '', birthQos: '0', birthRetain: 'false', birthPayload: '', birthMsg: {},
    closeTopic: '', closeQos: '0', closeRetain: 'false', closePayload: '', closeMsg: {},
    willTopic: '', willQos: '0', willRetain: 'false', willPayload: '', willMsg: {},
    userProps: '', sessionExpiry: '' },
];

writeFileSync(new URL('cooler-history.json', here), JSON.stringify(nodes, null, 4) + '\n');
```

- [ ] **Step 5: Generate the flow and run the tests**

Run: `node nodered/build-flow.mjs && node --test nodered/test/`
Expected: all 14 tests PASS.

- [ ] **Step 6: Write `nodered/README.md`**

```markdown
# Node-RED: 24-hour history

`cooler-history.json` keeps the last 24 hours of `cooler/data` (box temperature, humidity,
relay) in one-minute slots and republishes the whole window on `cooler/history` after every
live `cooler/data`. The app and the wall panel merge it into their trend, so they show a full
day even after being disconnected. The history lives in Node-RED's memory: a Node-RED restart
empties it and it refills over 24 hours. It is not retained, so if Node-RED stops, clients
simply see live data only.

## Install

1. In Node-RED: Menu → Import → select `cooler-history.json` → Import. It adds a
   "Cooler history" tab.
2. Open the "Cooler broker (local)" config node and set the Security username and password
   to Node-RED's broker login (see the ACL below). If you already have a broker config for this
   Mosquitto, select it in the three MQTT nodes instead and delete the imported one.
3. Deploy. Within 30 s, `mosquitto_sub -t cooler/history -C 1` shows a message.

## Broker

The flow connects to `localhost:1883` without TLS. Mosquitto must have a plain listener bound
to localhost only (`listener 1883 127.0.0.1`), never one reachable from the network.

ACL for Node-RED's login:

    user <node-red login>
    topic read cooler/data
    topic read cooler/availability
    topic write cooler/history

Add `topic read cooler/history` to the panel's and the app's logins.

## Editing

The Function node's code is `history.js`. After changing it run
`node nodered/build-flow.mjs && node --test nodered/test/`, then re-import.
```

- [ ] **Step 7: Update `README.md`**

In the topology diagram replace `    BRK -.-> NR[Node-RED<br/>alerts + history, planned]` with:

```
    BRK <--> NR[Node-RED<br/>24 h history]
```

In the topic table add after the `cooler/cmd` row:

```
| `cooler/history` | Node-RED publishes, not retained | JSON, `"v": 1`, the last 24 h in one-minute slots, after every live `cooler/data`; see [nodered/README.md](nodered/README.md) |
```

Replace the login table rows with:

```
| the controller's login (`mqtt_username`) | controller | read/write `cooler/#` |
| the panel's login (`panel_mqtt_username`) | CoolerPanel | read `cooler/data`, `cooler/availability`, `cooler/history`; write `cooler/cmd` |
| the app's login (`app_mqtt_username`) | CoolerApp | same as the panel's |
| Node-RED's login | Node-RED | read `cooler/data`, `cooler/availability`; write `cooler/history` |
```

In "Companion projects" replace the Node-RED bullet with:

```
- **nodered/** - Node-RED flow that keeps 24 h of history and republishes it on `cooler/history`. See [nodered/README.md](nodered/README.md). Alerts are still planned.
```

In "Repository layout" add after the `CoolerApp/` line:

```
nodered/           Node-RED 24 h history flow (history.js, build-flow.mjs, tests)
```

In "To do" replace item 6 with `6. Alerts in Node-RED.`

- [ ] **Step 8: Commit**

```bash
git add nodered README.md
git commit -m "feat(nodered): 24-hour history flow on cooler/history

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: App — parse the history and merge it into the trend

**Files:**
- Create: `APP/model/HistoryParser.kt`, `APPTEST/model/HistoryParserTest.kt`
- Modify: `APP/model/TrendBuffer.kt`, `APPTEST/model/TrendBufferTest.kt`

**Interfaces:**
- Consumes: `Sample(epochS: Long, tempC: Double, relay: Boolean?)` and `TrendBuffer.MIN_VALID_EPOCH` (existing, `TrendBuffer.kt`).
- Produces: `data class HistoryWindow(val fromS: Long, val toS: Long, val samples: List<Sample>)`; `object HistoryParser { const val VERSION = 1; fun parse(payload: String, nowS: Long): HistoryWindow? }`; `fun TrendBuffer.withHistory(w: HistoryWindow): TrendBuffer`.

- [ ] **Step 1: Write the failing tests** — `APPTEST/model/HistoryParserTest.kt`

```kotlin
package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class HistoryParserTest {
    private val t0 = 1_790_700_000L
    private val now = t0 + 180

    private fun body(
        v: Int = 1,
        t0: Long = this.t0,
        interval: Int = 60,
        temp: String = "4.2,null,4.4",
        hum: String = "80,81,null",
        relay: String = "0,1,1",
    ) = """{"v":$v,"t0":$t0,"interval_s":$interval,"temp":[$temp],"hum":[$hum],"relay":[$relay]}"""

    @Test
    fun parsesSlotsAndSkipsMissingTemperatures() {
        val w = HistoryParser.parse(body(), now)!!
        assertEquals(t0, w.fromS)
        assertEquals(t0 + 180, w.toS)
        assertEquals(listOf(Sample(t0, 4.2, false), Sample(t0 + 120, 4.4, true)), w.samples)
    }

    @Test
    fun nullRelayIsUnknown() {
        val w = HistoryParser.parse(body(temp = "4.2", hum = "80", relay = "null"), now)!!
        assertEquals(listOf(Sample(t0, 4.2, null)), w.samples)
    }

    @Test
    fun rejectsWrongVersionOrInterval() {
        assertNull(HistoryParser.parse(body(v = 2), now))
        assertNull(HistoryParser.parse(body(interval = 30), now))
    }

    @Test
    fun rejectsUnequalEmptyOrOversizedArrays() {
        assertNull(HistoryParser.parse(body(hum = "80"), now))
        assertNull(HistoryParser.parse(body(temp = "", hum = "", relay = ""), now))
        val big = List(1441) { "4" }.joinToString(",")
        assertNull(HistoryParser.parse(body(temp = big, hum = big, relay = big), t0 + 1441 * 60))
    }

    @Test
    fun rejectsMissingFieldsAndJunk() {
        assertNull(HistoryParser.parse("""{"v":1,"t0":$t0,"interval_s":60,"temp":[4]}""", now))
        assertNull(HistoryParser.parse("not json", now))
        assertNull(HistoryParser.parse("[]", now))
    }

    @Test
    fun rejectsAnUnsetControllerClockAndTheFuture() {
        assertNull(HistoryParser.parse(body(t0 = 12_345), now))
        assertNull(HistoryParser.parse(body(), t0 + 180 - 121))
        assertNotNull(HistoryParser.parse(body(), t0 + 180 - 120))
    }

    @Test
    fun futureCheckSkippedWhileOwnClockUnset() {
        assertNotNull(HistoryParser.parse(body(), 0))
    }
}
```

Append to `APPTEST/model/TrendBufferTest.kt`, inside the class:

```kotlin
    @Test
    fun historyReplacesItsSpanAndKeepsTheRest() {
        val b = TrendBuffer()
            .appended(1_000, 1.0, false)
            .appended(1_100, 2.0, false)
            .appended(1_500, 9.0, true)
        val w = HistoryWindow(1_050, 1_400, listOf(Sample(1_080, 3.0, true), Sample(1_140, 4.0, null)))
        assertEquals(
            listOf(Sample(1_000, 1.0, false), Sample(1_080, 3.0, true), Sample(1_140, 4.0, null), Sample(1_500, 9.0, true)),
            b.withHistory(w).all,
        )
    }

    @Test
    fun historyFillsAnEmptyBuffer() {
        val w = HistoryWindow(1_000, 1_120, listOf(Sample(1_000, 3.0, false), Sample(1_060, 3.1, true)))
        assertEquals(w.samples, TrendBuffer().withHistory(w).all)
    }

    @Test
    fun liveSamplesAfterTheHistoryStillAppend() {
        val w = HistoryWindow(1_000, 1_060, listOf(Sample(1_000, 3.0, false)))
        assertEquals(2, TrendBuffer().withHistory(w).appended(1_030, 3.2, true).all.size)
    }
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd CoolerApp && ./gradlew testDebugUnitTest --tests '*HistoryParserTest' --tests '*TrendBufferTest'`
Expected: compilation FAIL, `Unresolved reference: HistoryParser` / `HistoryWindow`.

- [ ] **Step 3: Write `APP/model/HistoryParser.kt`**

```kotlin
package ai.northtrail.cooler.model

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.longOrNull

/** `<base>/history` from Node-RED: [samples] cover `[fromS, toS)`; slots with no temperature are left out. */
data class HistoryWindow(val fromS: Long, val toS: Long, val samples: List<Sample>)

object HistoryParser {
    const val VERSION = 1
    private const val INTERVAL_S = 60L
    private const val MAX_SLOTS = 1440
    private const val MAX_AHEAD_S = 120L

    /** Null for anything that is not a sound v1 window. [nowS] is this phone's clock. */
    fun parse(payload: String, nowS: Long): HistoryWindow? {
        val o = runCatching { Json.parseToJsonElement(payload) }.getOrNull() as? JsonObject ?: return null
        if (o.prim("v")?.intOrNull != VERSION || o.prim("interval_s")?.longOrNull != INTERVAL_S) return null
        val t0 = o.prim("t0")?.longOrNull ?: return null
        if (t0 < TrendBuffer.MIN_VALID_EPOCH) return null
        val temp = o["temp"] as? JsonArray ?: return null
        val hum = o["hum"] as? JsonArray ?: return null
        val relay = o["relay"] as? JsonArray ?: return null
        val n = temp.size
        if (n == 0 || n > MAX_SLOTS || hum.size != n || relay.size != n) return null
        val toS = t0 + n * INTERVAL_S
        // An unset phone clock cannot judge "the future".
        if (nowS >= TrendBuffer.MIN_VALID_EPOCH && toS > nowS + MAX_AHEAD_S) return null
        val samples = (0 until n).mapNotNull { i ->
            val t = temp[i].number() ?: return@mapNotNull null
            val r = when (relay[i].number()?.toInt()) { 1 -> true; 0 -> false; else -> null }
            Sample(t0 + i * INTERVAL_S, t, r)
        }
        return HistoryWindow(t0, toS, samples)
    }

    private fun JsonObject.prim(key: String): JsonPrimitive? =
        (this[key] as? JsonPrimitive)?.takeUnless { it is JsonNull || it.isString }

    private fun JsonElement.number(): Double? =
        (this as? JsonPrimitive)?.takeUnless { it is JsonNull || it.isString }?.doubleOrNull
}
```

- [ ] **Step 4: Add `withHistory` to `APP/model/TrendBuffer.kt`** after `appended(...)`:

```kotlin
    /** Node-RED's record replaces whatever this buffer holds inside the window's span. */
    fun withHistory(w: HistoryWindow): TrendBuffer {
        val kept = all.filter { it.epochS < w.fromS || it.epochS >= w.toS }
        return TrendBuffer((kept + w.samples).sortedBy { it.epochS }.takeLast(CAPACITY))
    }
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd CoolerApp && ./gradlew testDebugUnitTest --tests '*HistoryParserTest' --tests '*TrendBufferTest'`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add CoolerApp/app/src/main/java/ai/northtrail/cooler/model/HistoryParser.kt \
        CoolerApp/app/src/main/java/ai/northtrail/cooler/model/TrendBuffer.kt \
        CoolerApp/app/src/test/java/ai/northtrail/cooler/model/HistoryParserTest.kt \
        CoolerApp/app/src/test/java/ai/northtrail/cooler/model/TrendBufferTest.kt
git commit -m "feat(app): parse cooler/history and merge it into the trend

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: App — subscribe to history (optional) and route it

**Files:**
- Modify: `APP/model/Topics.kt`, `APP/data/HiveMqSession.kt:25-35,106-122`, `APP/data/MqttRepository.kt:78-92`, `APP/CoolerController.kt:119-147`
- Test: `APPTEST/model/TopicsTest.kt`, `APPTEST/data/FakeBroker.kt`, `APPTEST/data/HiveMqSessionTest.kt`, `APPTEST/CoolerControllerTest.kt`

**Interfaces:**
- Consumes: `HistoryParser.parse(payload, nowS)`, `HistoryParser.VERSION`, `TrendBuffer.withHistory(w)` (Task 2).
- Produces: `Topics.history: String`, `Topics.optionalSubscriptions: List<String>`; `HiveMqSession(..., subscriptions, optionalSubscriptions: List<String> = emptyList(), onLink, onMessage)`; `FakeBroker(connackCode, publishAfterSubscribe, refuse: Set<String> = emptySet())`.

- [ ] **Step 1: Write the failing tests**

In `APPTEST/model/TopicsTest.kt` add `assertEquals("cooler/history", t.history)` to `defaultBaseIsCooler`, and add:

```kotlin
    @Test
    fun historyIsAnOptionalSubscription() {
        assertEquals(listOf("barn/history"), Topics("barn").optionalSubscriptions)
    }
```

In `APPTEST/data/FakeBroker.kt`: add the constructor parameter `private val refuse: Set<String> = emptySet(),` after `publishAfterSubscribe`, update the KDoc's "grants them" to "grants them (or refuses those in [refuse])", and replace

```kotlin
                    out.write(ByteArray(topics.size) { 0x01 })
```

with

```kotlin
                    out.write(ByteArray(topics.size) { if (topics[it] in refuse) 0x80.toByte() else 0x01 })
```

In `APPTEST/data/HiveMqSessionTest.kt` change the helper and the first test, and add two tests:

```kotlin
    private fun session(port: Int, topics: Topics = Topics()) = HiveMqSession(
        host = "127.0.0.1",
        port = port,
        username = "app-user",
        password = "x".toByteArray(),
        clientId = "test-${System.nanoTime()}",
        trustManagerFactory = null,
        subscriptions = topics.subscriptions,
        optionalSubscriptions = topics.optionalSubscriptions,
        onLink = { state, _, _ -> links += state },
        onMessage = { messages += it },
    )

    @Test
    fun subscribesToTheCoolerTopicsAndHistory() {
        FakeBroker().use { broker ->
            val s = session(broker.port)
            s.start()
            waitUntil { LinkState.CONNECTED in links }
            s.stop()
            assertTrue("links seen: $links", LinkState.CONNECTED in links)
            assertEquals(listOf("cooler/data", "cooler/availability", "cooler/history"), broker.subscribedTopics.toList())
        }
    }

    @Test
    fun refusedOptionalSubscriptionStillConnects() {
        FakeBroker(refuse = setOf("cooler/history")).use { broker ->
            val s = session(broker.port)
            s.start()
            waitUntil { LinkState.CONNECTED in links }
            s.stop()
            assertFalse("links seen: $links", LinkState.REJECTED in links)
        }
    }

    @Test
    fun refusedRequiredSubscriptionIsRejected() {
        FakeBroker(refuse = setOf("cooler/data")).use { broker ->
            val s = session(broker.port)
            s.start()
            waitUntil { LinkState.REJECTED in links }
            s.stop()
            assertFalse("links seen: $links", LinkState.CONNECTED in links)
        }
    }
```

(Delete the old `subscribesToExactlyTheTwoCoolerTopics`; add `import org.junit.Assert.assertFalse`.)

In `APPTEST/CoolerControllerTest.kt` add these two tests (`Sample` is already imported):

```kotlin
    @Test
    fun historyFillsTheTrend() = runTest {
        val c = controller()
        online()
        emit(
            "cooler/history",
            """{"v":1,"t0":1790700000,"interval_s":60,"temp":[4.2,null,4.4],"hum":[80,81,82],"relay":[0,1,1]}""",
        )
        assertEquals(listOf(Sample(1_790_700_000, 4.2, false), Sample(1_790_700_120, 4.4, true)), c.ui.value.trend.all)
    }

    @Test
    fun aBadHistoryIsIgnoredAndReported() = runTest {
        val c = controller()
        online()
        emit("cooler/history", """{"v":7}""")
        assertTrue(c.ui.value.trend.all.isEmpty())
        assertEquals(listOf("ignored cooler/history: not cooler/history v1"), ignored)
    }
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd CoolerApp && ./gradlew testDebugUnitTest`
Expected: compilation FAIL, `Unresolved reference: history` / `optionalSubscriptions`.

- [ ] **Step 3: Implement**

`APP/model/Topics.kt`, replace the body after `val cmd`:

```kotlin
    val history = "$b/history"

    /** Exact topics only: the broker login may read nothing else. A refusal of these is fatal. */
    val subscriptions: List<String> = listOf(data, availability)

    /** Wanted but not needed: a broker ACL without `history` refuses it, and the app works on without. */
    val optionalSubscriptions: List<String> = listOf(history)
```

`APP/data/HiveMqSession.kt`: add the constructor parameter after `subscriptions`:

```kotlin
    private val optionalSubscriptions: List<String> = emptyList(),
```

and in `subscribe()` replace `subscriptions.map {` with `(subscriptions + optionalSubscriptions).map {`, and the refusal branch with:

```kotlin
                // Only the required topics come first in the SUBACK; a refused optional one is not fatal.
                ack.returnCodes.take(subscriptions.size).any { it == Mqtt3SubAckReturnCode.FAILURE } ->
```

`APP/data/MqttRepository.kt`: after `subscriptions = Topics(config.base).subscriptions,` add

```kotlin
            optionalSubscriptions = Topics(config.base).optionalSubscriptions,
```

`APP/CoolerController.kt`: add `import ai.northtrail.cooler.model.HistoryParser`, and in `onMessage` add a branch after the `t.data -> { ... }` branch:

```kotlin
            t.history -> {
                val w = HistoryParser.parse(m.payload, now / 1_000)
                if (w == null) {
                    onIgnored("ignored ${m.topic}: not cooler/history v${HistoryParser.VERSION}")
                    return
                }
                update { it.copy(trend = it.trend.withHistory(w)) }
            }
```

- [ ] **Step 4: Run all app unit tests**

Run: `cd CoolerApp && ./gradlew testDebugUnitTest`
Expected: PASS (all classes, including `BrokerProbeTest`, unchanged).

- [ ] **Step 5: Commit**

```bash
git add CoolerApp/app/src
git commit -m "feat(app): subscribe to cooler/history; a refused history subscription is not fatal

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Panel — parse the history and merge it into the ring

**Files:**
- Create: `CoolerPanel/shared/model/history_msg.h`, `CoolerPanel/shared/model/history_msg.cpp`, `CoolerPanel/tests/test_history_msg.cpp`
- Modify: `CoolerPanel/shared/model/history.h`, `CoolerPanel/shared/model/history.cpp`, `CoolerPanel/tests/test_history.cpp`, `CoolerPanel/CMakeLists.txt:31-44`

**Interfaces:**
- Consumes: `struct Sample { int64_t t; int16_t temp_c10; int16_t rh_c10; uint8_t ac; }`, `class History` (existing).
- Produces: `void History::merge_window(int64_t from, int64_t to, const Sample* s, size_t n)`; `struct HistoryMsg { int64_t from, to; std::vector<Sample> samples; }`; `bool history_msg_parse(const char* json, size_t len, int64_t now, HistoryMsg& out)`; `bool history_adds_coverage(const History& h, const HistoryMsg& m)`.

- [ ] **Step 1: Write the failing tests**

Append to `CoolerPanel/tests/test_history.cpp`:

```cpp
TEST_CASE("merge_window replaces its span and keeps samples outside it") {
    History h;
    REQUIRE(h.init(10));
    h.maybe_append(100, 1.0f, 10.0f, 30);
    h.maybe_append(130, 2.0f, 20.0f, 30);
    h.maybe_append(400, 3.0f, 30.0f, 30);
    h.maybe_append(900, 9.0f, 90.0f, 30);
    const Sample s[] = {{360, 50, 500, 1}, {420, 60, 600, 0}};
    h.merge_window(300, 600, s, 2);
    REQUIRE(h.size() == 5);
    CHECK(h.at(0).t == 100);
    CHECK(h.at(1).t == 130);
    CHECK(h.at(2).t == 360);
    CHECK(h.at(2).ac == 1);
    CHECK(h.at(3).t == 420);
    CHECK(h.at(4).t == 900);
    // The ring still appends after a merge.
    CHECK(h.maybe_append(960, 9.5f, 95.0f, 30));
    CHECK(h.newest_epoch() == 960);
}

TEST_CASE("merge_window keeps the newest when over capacity") {
    History h;
    REQUIRE(h.init(3));
    h.maybe_append(100, 1.0f, 10.0f, 30);
    h.maybe_append(200, 2.0f, 20.0f, 30);
    const Sample s[] = {{300, 30, 300, 0}, {360, 36, 360, 0}, {420, 42, 420, 0}};
    h.merge_window(300, 480, s, 3);
    REQUIRE(h.size() == 3);
    CHECK(h.oldest_epoch() == 300);
    CHECK(h.newest_epoch() == 420);
}

TEST_CASE("merge_window into an empty ring") {
    History h;
    REQUIRE(h.init(10));
    const Sample s[] = {{300, 30, 300, 1}};
    h.merge_window(300, 360, s, 1);
    REQUIRE(h.size() == 1);
    CHECK(h.at(0).temp_c10 == 30);
}
```

Create `CoolerPanel/tests/test_history_msg.cpp`:

```cpp
#include <doctest/doctest.h>
#include "history_msg.h"
#include "history.h"
#include <cstdio>
#include <string>

static const int64_t NOW = 1700000000;
static const int64_t T0 = 1699999380;   // a minute boundary, 620 s before NOW

static std::string body(int64_t t0 = T0, const char* temp = "4.2,null,4.4",
                        const char* hum = "80,81,82", const char* relay = "0,1,1",
                        int v = 1, int interval = 60) {
    char b[1024];
    std::snprintf(b, sizeof b,
                  "{\"v\":%d,\"t0\":%lld,\"interval_s\":%d,\"temp\":[%s],\"hum\":[%s],\"relay\":[%s]}",
                  v, (long long)t0, interval, temp, hum, relay);
    return b;
}

static bool parse(const std::string& s, HistoryMsg& m, int64_t now = NOW) {
    return history_msg_parse(s.data(), s.size(), now, m);
}

TEST_CASE("parses slots and skips those without temperature or humidity") {
    HistoryMsg m;
    REQUIRE(parse(body(T0, "4.2,null,4.4,5.0", "80,81,82,null", "0,1,1,0"), m));
    CHECK(m.from == T0);
    CHECK(m.to == T0 + 240);
    REQUIRE(m.samples.size() == 2);
    CHECK(m.samples[0].t == T0);
    CHECK(m.samples[0].temp_c10 == 42);
    CHECK(m.samples[0].rh_c10 == 800);
    CHECK(m.samples[0].ac == 0);
    CHECK(m.samples[1].t == T0 + 120);
    CHECK(m.samples[1].temp_c10 == 44);
    CHECK(m.samples[1].ac == 1);
}

TEST_CASE("rejects anything that is not a sound v1 window") {
    HistoryMsg m;
    CHECK_FALSE(parse(body(T0, "4.2", "80", "0", 2), m));            // version
    CHECK_FALSE(parse(body(T0, "4.2", "80", "0", 1, 30), m));        // interval
    CHECK_FALSE(parse(body(T0, "4.2,4.3", "80", "0,0"), m));         // unequal
    CHECK_FALSE(parse(body(T0, "", "", ""), m));                     // empty
    CHECK_FALSE(parse(body(12345, "4.2", "80", "0"), m));            // controller-side clock unset
    CHECK_FALSE(parse(body(NOW + 600, "4.2", "80", "0"), m));        // from the future
    const std::string junk = "not json";
    CHECK_FALSE(parse(junk, m));
    const std::string partial = "{\"v\":1,\"t0\":1699999380,\"interval_s\":60,\"temp\":[4]}";
    CHECK_FALSE(parse(partial, m));
}

TEST_CASE("future check is skipped while the panel clock is unset") {
    HistoryMsg m;
    CHECK(parse(body(NOW + 600, "4.2", "80", "0"), m, 0));
}

TEST_CASE("coverage: a message adds something only where the panel has no sample") {
    HistoryMsg m;
    REQUIRE(parse(body(), m));          // samples at T0 and T0+120; T0+60 is null

    History empty;
    REQUIRE(empty.init(10));
    CHECK(history_adds_coverage(empty, m));

    History full;
    REQUIRE(full.init(10));
    full.maybe_append(T0 + 10, 4.0f, 80.0f, 30);
    full.maybe_append(T0 + 130, 4.0f, 80.0f, 30);
    CHECK_FALSE(history_adds_coverage(full, m));   // T0+60 is missing on both sides

    History gap;
    REQUIRE(gap.init(10));
    gap.maybe_append(T0 + 10, 4.0f, 80.0f, 30);
    CHECK(history_adds_coverage(gap, m));          // panel lacks T0+120
}
```

Add `tests/test_history_msg.cpp` to the `add_executable(cooler_tests ...)` list in `CoolerPanel/CMakeLists.txt`, after `tests/test_history.cpp`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd CoolerPanel && cmake -S . -B build && cmake --build build -j`
Expected: compile FAIL, `history_msg.h: No such file` and `no member named 'merge_window'`.

- [ ] **Step 3: Implement `merge_window`**

In `CoolerPanel/shared/model/history.h`, in `public:` after `clear();`:

```cpp
    // Replace every sample in [from, to) with the n given ones (sorted by t,
    // all inside [from, to)), keeping own samples outside that span. If the
    // result is over capacity the oldest go.
    void merge_window(int64_t from, int64_t to, const Sample* s, size_t n);
```

In `CoolerPanel/shared/model/history.cpp` add `#include <vector>` and:

```cpp
void History::merge_window(int64_t from, int64_t to, const Sample* s, size_t n) {
    if (!buf_) return;
    std::vector<Sample> out;
    out.reserve(count_ + n);
    size_t i = 0;
    for (; i < count_ && at(i).t < from; i++) out.push_back(at(i));
    for (size_t k = 0; k < n; k++) out.push_back(s[k]);
    for (; i < count_; i++)
        if (at(i).t >= to) out.push_back(at(i));
    size_t start = out.size() > cap_ ? out.size() - cap_ : 0;
    count_ = out.size() - start;
    head_ = 0;
    for (size_t k = 0; k < count_; k++) buf_[k] = out[start + k];
}
```

- [ ] **Step 4: Write `CoolerPanel/shared/model/history_msg.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "history.h"

// <base>/history from Node-RED: the last 24 h in one-minute slots. samples
// cover [from, to); slots without temperature or humidity are left out.
struct HistoryMsg {
    int64_t from = 0;
    int64_t to = 0;
    std::vector<Sample> samples;
};

// False for anything that is not a sound v1 window. now is the panel's clock;
// the "not from the future" check is skipped while it is unset.
bool history_msg_parse(const char* json, size_t len, int64_t now, HistoryMsg& out);

// True when m has a sample in a minute of [m.from, m.to) where h has none.
// /history arrives with every /data, so a panel that has been connected all
// along skips the merge rather than rebuilding its ring twice a minute.
bool history_adds_coverage(const History& h, const HistoryMsg& m);
```

- [ ] **Step 5: Write `CoolerPanel/shared/model/history_msg.cpp`**

```cpp
#include "history_msg.h"
#include <ArduinoJson.h>
#include <cmath>

static constexpr int64_t kStep = 60;
static constexpr size_t kMaxSlots = 1440;
static constexpr int64_t kMaxAhead = 120;
static constexpr int64_t kMinValidEpoch = 1600000000;   // 2020: before this a clock is unset

static int16_t c10(double v) {
    double scaled = v * 10.0;
    if (scaled > 32767.0) scaled = 32767.0;
    if (scaled < -32768.0) scaled = -32768.0;
    return (int16_t)std::lround(scaled);
}

bool history_msg_parse(const char* json, size_t len, int64_t now, HistoryMsg& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    if (o.isNull()) return false;
    if (!o["v"].is<int>() || o["v"].as<int>() != 1) return false;
    if (!o["interval_s"].is<int>() || o["interval_s"].as<int>() != kStep) return false;
    if (!o["t0"].is<int64_t>()) return false;
    const int64_t t0 = o["t0"].as<int64_t>();
    if (t0 < kMinValidEpoch) return false;
    JsonArrayConst temp = o["temp"].as<JsonArrayConst>();
    JsonArrayConst hum = o["hum"].as<JsonArrayConst>();
    JsonArrayConst relay = o["relay"].as<JsonArrayConst>();
    if (temp.isNull() || hum.isNull() || relay.isNull()) return false;
    const size_t n = temp.size();
    if (n == 0 || n > kMaxSlots || hum.size() != n || relay.size() != n) return false;
    const int64_t to = t0 + (int64_t)n * kStep;
    if (now >= kMinValidEpoch && to > now + kMaxAhead) return false;

    out.from = t0;
    out.to = to;
    out.samples.clear();
    out.samples.reserve(n);
    for (size_t i = 0; i < n; i++) {
        JsonVariantConst t = temp[i], h = hum[i], r = relay[i];
        if (!t.is<double>() || !h.is<double>()) continue;
        Sample s{t0 + (int64_t)i * kStep, c10(t.as<double>()), c10(h.as<double>()),
                 (uint8_t)(r.is<int>() && r.as<int>() == 1 ? 1 : 0)};
        out.samples.push_back(s);
    }
    return true;
}

bool history_adds_coverage(const History& h, const HistoryMsg& m) {
    if (m.to <= m.from) return false;
    std::vector<bool> have((size_t)((m.to - m.from) / kStep), false);
    for (size_t i = 0; i < h.size(); i++) {
        const int64_t t = h.at(i).t;
        if (t >= m.from && t < m.to) have[(size_t)((t - m.from) / kStep)] = true;
    }
    for (const Sample& s : m.samples)
        if (!have[(size_t)((s.t - m.from) / kStep)]) return true;
    return false;
}
```

- [ ] **Step 6: Build and run the tests**

Run: `cd CoolerPanel && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `unit` and `portability_guard` PASS.

- [ ] **Step 7: Commit**

```bash
git add CoolerPanel/shared/model/history.h CoolerPanel/shared/model/history.cpp \
        CoolerPanel/shared/model/history_msg.h CoolerPanel/shared/model/history_msg.cpp \
        CoolerPanel/tests/test_history.cpp CoolerPanel/tests/test_history_msg.cpp CoolerPanel/CMakeLists.txt
git commit -m "feat(panel): parse cooler/history and merge it into the history ring

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Panel — subscribe and route `<base>/history`

**Files:**
- Modify: `CoolerPanel/shared/model/mqtt_router.h`, `CoolerPanel/shared/model/mqtt_router.cpp`, `CoolerPanel/shared/app.cpp:47-80`, `CoolerPanel/device/src/mqtt_pubsub.cpp:36,62-68`, `CoolerPanel/sim/mqtt_mosq.cpp:23-26`, `CoolerPanel/README.md`
- Test: `CoolerPanel/tests/test_app_wiring.cpp`

**Interfaces:**
- Consumes: `history_msg_parse`, `history_adds_coverage`, `History::merge_window` (Task 4); `platform_epoch_utc()` returns 1700000000 in tests (`tests/test_platform_stub.cpp`).
- Produces: `bool router_is_leaf(const char* topic, const char* leaf)`.

- [ ] **Step 1: Write the failing tests** — append to `CoolerPanel/tests/test_app_wiring.cpp`

```cpp
TEST_CASE("a /history message fills the minutes the panel never saw") {
    app_init_history();
    const std::string topic = std::string(router_prefix()) + "/history";
    // platform_epoch_utc() is 1700000000 under test; t0 is 620 s earlier.
    const std::string body =
        "{\"v\":1,\"t0\":1699999380,\"interval_s\":60,"
        "\"temp\":[4.2,null,4.4],\"hum\":[80,81,82],\"relay\":[0,1,1]}";
    app_on_mqtt_message(topic.c_str(), (const uint8_t*)body.data(), body.size());
    REQUIRE(panel_history().size() == 2);
    CHECK(panel_history().at(0).t == 1699999380);
    CHECK(panel_history().at(1).ac == 1);

    // Delivered again, it adds no coverage: nothing changes.
    app_on_mqtt_message(topic.c_str(), (const uint8_t*)body.data(), body.size());
    CHECK(panel_history().size() == 2);
}

TEST_CASE("a bad /history message leaves history alone") {
    app_init_history();
    const std::string topic = std::string(router_prefix()) + "/history";
    const std::string body = "{\"v\":9}";
    app_on_mqtt_message(topic.c_str(), (const uint8_t*)body.data(), body.size());
    CHECK(panel_history().size() == 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd CoolerPanel && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `unit` FAIL in "a /history message fills ..." (`panel_history().size() == 2` is 0).

- [ ] **Step 3: Implement**

`CoolerPanel/shared/model/mqtt_router.h`, after `router_prefix`:

```cpp
// True if topic is exactly "<prefix>/<leaf>".
bool router_is_leaf(const char* topic, const char* leaf);
```

`CoolerPanel/shared/model/mqtt_router.cpp`, after `router_prefix`:

```cpp
bool router_is_leaf(const char* topic, const char* leaf) {
    if (!topic || !leaf) return false;
    size_t pn = std::strlen(s_prefix);
    return std::strncmp(topic, s_prefix, pn) == 0 && topic[pn] == '/' &&
           std::strcmp(topic + pn + 1, leaf) == 0;
}
```

`CoolerPanel/shared/app.cpp`: add `#include "history_msg.h"` after `#include "history.h"`, and make this the first statement of `app_on_mqtt_message`:

```cpp
    // /history (Node-RED, after every /data) is not cooler state: it only
    // fills minutes of the trend this panel did not see itself.
    if (router_is_leaf(topic, "history")) {
        HistoryMsg m;
        if (history_msg_parse((const char*)payload, len, platform_epoch_utc(), m) &&
            history_adds_coverage(g_hist, m))
            g_hist.merge_window(m.from, m.to, m.samples.data(), m.samples.size());
        return;
    }
```

`CoolerPanel/device/src/mqtt_pubsub.cpp`: change `s_mqtt.setBufferSize(4096);             // stats/session payloads > default 256` to

```cpp
    s_mqtt.setBufferSize(24576);            // /history from Node-RED is ~14 KB
```

and replace the subscription block comment and calls with:

```cpp
        // Explicit subscriptions rather than a "<base>/#" wildcard: the
        // panel only needs /data, /availability and /history, and a
        // wildcard would also pick up any /cmd echo or future subtree this
        // device itself publishes to. A broker ACL without /history just
        // never delivers it; the panel then trends from live /data alone.
        s_mqtt.subscribe((s_cfg.mqtt_base + "/data").c_str(), 1);
        s_mqtt.subscribe((s_cfg.mqtt_base + "/availability").c_str(), 1);
        s_mqtt.subscribe((s_cfg.mqtt_base + "/history").c_str(), 0);
```

`CoolerPanel/sim/mqtt_mosq.cpp`: change the comment to "The same exact topics the device subscribes to. The panel's broker login may read only these (no <base>/# wildcard)." and add after the `/availability` line:

```cpp
        mosquitto_subscribe(m, nullptr, (g_base + "/history").c_str(), 0);
```

In `CoolerPanel/README.md`, where it describes the trend, add one sentence: "When Node-RED's `cooler/history` is available (see `../nodered/README.md`), the trend also fills in the last 24 hours the panel missed, for example after a reboot."

- [ ] **Step 4: Run all panel tests and the device build**

Run: `cd CoolerPanel && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `unit` and `portability_guard` PASS.

Run: `cd CoolerPanel/device && pio run`
Expected: `SUCCESS`. Note RAM/flash usage from the output for the final report.

- [ ] **Step 5: Commit**

```bash
git add CoolerPanel/shared CoolerPanel/device/src/mqtt_pubsub.cpp CoolerPanel/sim/mqtt_mosq.cpp \
        CoolerPanel/tests/test_app_wiring.cpp CoolerPanel/README.md
git commit -m "feat(panel): subscribe to cooler/history and fill the trend from it

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Full verification

- [ ] **Step 1: Run every suite**

```bash
node --test nodered/test/
(cd CoolerApp && ./gradlew testDebugUnitTest)
(cd CoolerPanel && cmake --build build -j && ctest --test-dir build --output-on-failure)
(cd controller && cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure)   # untouched; must still pass
```

Expected: all PASS; `git status` shows nothing unexpected (the root PDF stays untracked).

- [ ] **Step 2: Hand-off notes for the user** (no code): import the flow, set Node-RED's broker login, add the four ACL lines from `nodered/README.md`, reload Mosquitto, then flash the panel and install the app.
