// Runs the "dehum history" Function node exactly as exported in dehum-history.json,
// with a fake flow context and clock. Run: node --test nodered/test/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const here = new URL('..', import.meta.url);
const nodes = JSON.parse(readFileSync(new URL('dehum-history.json', here), 'utf8'));
const fnNode = nodes.find((n) => n.type === 'function' && n.name === 'dehum history');
const T = 1_790_700_000; // a minute boundary

function harness() {
  const store = new Map();
  const flow = { get: (k) => store.get(k), set: (k, v) => store.set(k, v) };
  let nowMs = T * 1000;
  const FakeDate = { now: () => nowMs };
  const run = new Function('msg', 'flow', 'node', 'Date', fnNode.func);
  return {
    store,
    at(s) { nowMs = s * 1000; },
    data(payload, { retain = false, topic = 'dehum/data' } = {}) {
      const out = run({ topic, payload, retain }, flow, {}, FakeDate);
      return out ? { ...out, body: JSON.parse(out.payload) } : null;
    },
    avail(text) { return run({ topic: 'dehum/availability', payload: text }, flow, {}, FakeDate); },
  };
}
const d = (humidity = 55, temp = 23.6, relay = 1) => ({ v: 1, humidity, temp, relay });

test('the exported function is dehum-history.js verbatim', () => {
  assert.equal(fnNode.func, readFileSync(new URL('dehum-history.js', here), 'utf8'));
});

test('subscribes to exactly dehum data and availability, publishes dehum/history unretained', () => {
  const ins = nodes.filter((n) => n.type === 'mqtt in');
  assert.deepEqual(ins.map((n) => n.topic).sort(), ['dehum/availability', 'dehum/data']);
  assert.ok(ins.every((n) => n.rap === false));
  const out = nodes.find((n) => n.type === 'mqtt out');
  assert.equal(out.topic, 'dehum/history');
  assert.equal(out.retain, 'false');
});

test('shares the cooler flow broker node and has no node id of its own in common otherwise', () => {
  const cooler = JSON.parse(readFileSync(new URL('cooler-history.json', here), 'utf8'));
  const broker = cooler.find((n) => n.type === 'mqtt-broker');
  assert.ok(nodes.filter((n) => n.type !== 'mqtt-broker').every((n) => n.type === 'tab' || n.broker === undefined || n.broker === broker.id));
  const ids = new Set(cooler.map((n) => n.id));
  assert.ok(nodes.filter((n) => n.type !== 'mqtt-broker').every((n) => !ids.has(n.id)));
});

test('one live message makes a one-slot window', () => {
  const h = harness();
  const out = h.data(d(55.4, 23.64, 1));
  assert.equal(out.topic, 'dehum/history');
  assert.equal(out.retain, false);
  assert.deepEqual(out.body, { v: 1, t0: T, interval_s: 60, hum: [55], temp: [23.6], relay: [1] });
});

test('a null reading keeps the relay and nulls hum and temp', () => {
  const out = harness().data(d(null, null, 0));
  assert.deepEqual(out.body.hum, [null]);
  assert.deepEqual(out.body.temp, [null]);
  assert.deepEqual(out.body.relay, [0]);
});

test('retained replays, other versions and offline are ignored', () => {
  const h = harness();
  assert.equal(h.data(d(), { retain: true }), null);
  assert.equal(h.data({ ...d(), v: 2 }), null);
  assert.equal(h.data(d(), { topic: 'cooler/data' }), null);
  h.avail('offline');
  assert.equal(h.data(d()), null);
  h.avail('online');
  assert.notEqual(h.data(d()), null);
});

test('missing minutes become null slots', () => {
  const h = harness();
  h.data(d(55));
  h.at(T + 120);
  const out = h.data(d(57));
  assert.deepEqual(out.body.hum, [55, null, 57]);
});

test('keeps 1440 slots and drops the oldest', () => {
  const h = harness();
  for (let i = 0; i <= 1440; i++) { h.at(T + i * 60); h.data(d(50)); }
  const out = h.data(d(51));
  assert.equal(out.body.hum.length, 1440);
  assert.equal(out.body.t0, T + 60);
});

test('keeps its own flow keys apart from the cooler flow', () => {
  const h = harness();
  h.avail('offline');
  assert.equal(h.store.get('avail'), undefined);
  assert.equal(h.store.get('dehum_avail'), 'offline');
});
