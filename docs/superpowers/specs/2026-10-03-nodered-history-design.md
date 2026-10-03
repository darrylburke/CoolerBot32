# 24-Hour History from Node-RED — Design

**Date:** 2026-10-03
**Status:** Draft, awaiting review
**Touches:** a new Node-RED flow (`nodered/`), CoolerApp, CoolerPanel, the broker ACL. The controller firmware is unchanged.

## 0. Summary

Node-RED, which already runs next to the broker, subscribes to `cooler/data` and keeps the
last 24 hours of box temperature, humidity and relay state in one-minute slots. After every
live `cooler/data` it publishes the whole window, not retained, on `cooler/history`. The app
and the panel subscribe to it and merge it into their trend, so a client that was
disconnected shows a full day as soon as the next `cooler/data` arrives (at most about 30 s).

### Goals / success criteria
- The app, opened after hours closed, shows the last 24 h of temperature with relay colouring
  within about 30 s of connecting.
- The panel, after a reboot, shows the last 24 h on its trend within about 30 s of connecting.
- Gaps in the record (controller offline, SHT30 fault) show as gaps, never as interpolated lines.
- If Node-RED is down, nothing stale is served: no history is published, and clients fall back
  to what they collect live, exactly as today.

### Non-goals
- Surviving a Node-RED restart. The history lives in flow memory and refills over 24 h.
- More than 24 h, downsampling or long-term storage.
- A humidity chart in the app. Humidity is carried for the panel only.
- Any controller firmware change. `temp_hist` / `hum_hist` in `cooler/data` stay as they are.

## 1. Decisions taken

| Decision | Choice | Reason |
|---|---|---|
| Storage | Node-RED flow context, in memory | Restarts are rare; no Redis to run and secure for a ~14 KB array. |
| Retain | Off | A retained history would outlive Node-RED and be served stale. |
| Delivery | Publish the full window after every live `cooler/data` | Clients only subscribe; no request topic, no client publish rights. |
| Window | 24 h at 60 s spacing, 1,440 slots | ~14 KB per message; fits the panel comfortably. |
| Fields | temp, humidity, relay | The app and panel both colour the trend by relay; the panel stores humidity. |

## 2. Message: `cooler/history`

Published by Node-RED, QoS 0, retain off, after every live `cooler/data`.

```json
{"v":1, "t0":1790700000, "interval_s":60,
 "temp":[3.4,3.5,null,3.6],
 "hum":[78,79,null,80],
 "relay":[0,1,null,1]}
```

- `t0` is the epoch second at the start of the oldest slot, a multiple of 60. Slot *i* covers
  `[t0 + i*60, t0 + (i+1)*60)`.
- The three arrays always have the same length *n*, 1 ≤ *n* ≤ 1440. The newest slot is the
  current minute.
- `temp` is °C to one decimal, `hum` is %RH as a whole number, `relay` is 0 or 1.
- `null` means no reading for that slot. A slot with no `cooler/data` at all is null in every
  array. A slot whose `cooler/data` carried `"temp":null` (SHT30 fault) has null temp and hum
  but a real relay value.
- Consumers must check `v == 1`, that the arrays are of equal length, and that
  `t0 + n*60` is not in the future by more than 120 s; otherwise the message is ignored.
- Size: about 13–14 KB for a full day.

## 3. Node-RED flow

Shipped as `nodered/cooler-history.json` for import. It contains no credentials: the broker
node has empty username and password, filled in after import. The Redis nodes from the draft
flow are dropped.

Nodes:
1. `mqtt in` on `cooler/data` (auto-detect JSON) and `mqtt in` on `cooler/availability`.
   Not `cooler/#`, which would feed `cooler/history` back in.
2. One Function node, **history**, holding all the logic.
3. `mqtt out` on `cooler/history`, retain false, QoS 0.

The **history** Function node:
- On `cooler/availability`: store `online` / `offline` in flow context. Output nothing.
- On `cooler/data`:
  - Drop it if `msg.retain` is true (a replay on Node-RED's connect, possibly hours old) or the
    stored availability is `offline`.
  - `slot = floor(now / 60) * 60` from Node-RED's clock.
  - Write `{temp, hum, relay}` into that slot; a later message in the same minute overwrites
    (last value wins). `temp` is rounded to 0.1, `hum` to 1; a null or missing value stays null.
  - Missing minutes between the previous newest slot and this one become null slots.
  - Slots older than `slot - 1439*60` are dropped.
  - If the clock went backwards (newest stored slot is after `slot`), clear the window and start
    again from this message.
  - Output the `cooler/history` payload.
- The window lives in flow context under one key, as three arrays plus `t0`.

### Broker
Node-RED connects to `localhost:1883` without TLS. This assumes Mosquitto has a plain
listener bound to localhost only. Node-RED gets its own login.

## 4. CoolerApp

- `model/Topics.kt`: add `history = "$b/history"` and include it in `subscriptions`.
- New `model/HistoryParser.kt`: payload → `List<Sample>` (`epochS`, `tempC`, `relay`), skipping
  slots whose temp is null; returns null for an invalid message (section 2 checks).
- `model/TrendBuffer.kt`: new `withHistory(samples: List<Sample>, fromS: Long, toS: Long)`.
  Samples already held inside `[fromS, toS)` are replaced by the history's; samples outside
  are kept; capacity and ordering rules as today.
- `CoolerController.kt`: on a `history` message, `trend = trend.withHistory(...)`.
- Humidity is parsed past and ignored.

Tests (JUnit, `app/src/test`): parser with nulls, unequal arrays, wrong `v`, missing fields,
future `t0`; merge replacing an overlap, keeping newer live samples, keeping older samples,
an empty buffer.

## 5. CoolerPanel

- `device/src/mqtt_pubsub.cpp`: subscribe to `<base>/history`; `setBufferSize(24576)`.
- `shared/model/mqtt_router.*`: route `<base>/history` to a new parser
  (`shared/model/history_msg.*`) that validates per section 2 and yields samples
  (`t`, `temp_c10`, `rh_c10`, `ac`), skipping slots whose temp is null.
- `shared/model/history.*`: new `merge_window(int64_t from, int64_t to, const Sample* s, size_t n)`.
  It rebuilds the ring as: own samples older than `from`, then the given samples, then own
  samples at or after `to`; oldest are dropped if over capacity.
- `shared/app.cpp`: after a valid history message, call `merge_window` only if it adds
  something: `from` is older than the oldest own sample within the last 24 h, or the message
  has more non-null slots in `[from, to)` than the panel holds there. In steady state (panel
  always connected) it is skipped.
- Live sampling from `cooler/data` is unchanged (30 s gate).

Tests (doctest, `tests/`): `merge_window` keeping older own samples, replacing the overlap,
keeping newer own samples, capacity overflow; parser validation; the skip rule in
`test_app_wiring.cpp`.

## 6. Broker ACL (README)

| Login | Used by | ACL |
|---|---|---|
| Node-RED's login | Node-RED | read `cooler/data`, `cooler/availability`; write `cooler/history` |
| the panel's login | CoolerPanel | read `cooler/data`, `cooler/availability`, `cooler/history`; write `cooler/cmd` |
| the app's login | CoolerApp | same as the panel's |

`cooler/history` is added to the README's topic table.

## 7. Testing the flow

`nodered/test/` holds a Node script (`node --test`) that loads `nodered/cooler-history.json`,
extracts the **history** Function node's source, and runs it with a fake `flow` context and a
controllable clock. Cases: first message, same-minute overwrite, gap filling, 24 h roll-off,
retained replay ignored, offline ignored, SHT30 fault (null temp, real relay), clock going
backwards, rounding.

## 8. Order of work

1. Node-RED flow and its tests; README topic and ACL rows. Usable alone: watch `cooler/history`.
2. CoolerApp.
3. CoolerPanel.

## 9. Risks

- **Panel RAM.** The 24 KB MQTT buffer and a temporary sample array (1,440 × 16 bytes) come
  from PSRAM; confirm with the boot heap log.
- **Phone data.** ~14 KB per `cooler/data` while the app is open, about 1.2–2 MB per hour.
- **Clock skew.** Node-RED's clock stamps the slots; the app and panel use their own clocks
  for live samples. NTP on all three keeps this within seconds; the merge replaces by time
  span, so small skew only shifts the boundary.
