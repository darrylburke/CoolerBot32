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

test('subscribes to exactly data and availability, retain flag only on replays', () => {
  const ins = nodes.filter((n) => n.type === 'mqtt in');
  assert.deepEqual(ins.map((n) => n.topic).sort(), ['cooler/availability', 'cooler/data']);
  // rap false: under MQTT v5 "retain as published" would keep retain=1 on every
  // live forward of the retained cooler/data, and history.js would drop them all.
  assert.ok(ins.every((n) => n.rap === false));
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
