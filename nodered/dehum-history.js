// Keeps the last 24 h of dehum/data (humidity, temperature, relay) in one-minute
// slots (flow memory) and emits the whole window for dehum/history after every
// live message. Same shape as history.js; its own flow keys.
const STEP = 60, SLOTS = 1440;

if (msg.topic === 'dehum/availability') {
    flow.set('dehum_avail', String(msg.payload).trim());
    return null;
}
if (msg.topic !== 'dehum/data') return null;
// A retained copy is the broker replaying its last message when Node-RED
// (re)connects; it may be hours old.
if (msg.retain || flow.get('dehum_avail') === 'offline') return null;
const d = msg.payload;
if (!d || typeof d !== 'object' || d.v !== 1) return null;

const num = (x, places) => {
    if (typeof x !== 'number' || !isFinite(x)) return null;
    const f = 10 ** places;
    return Math.round(x * f) / f;
};
const slot = Math.floor(Date.now() / 1000 / STEP) * STEP;
const fresh = () => ({ t0: slot, hum: [], temp: [], relay: [] });

let h = flow.get('dehum_hist') || fresh();
let newest = h.t0 + (h.hum.length - 1) * STEP;   // t0 - STEP when empty
// Clock went backwards, or the gap is longer than the window: start again.
if (newest > slot || slot - newest > SLOTS * STEP) { h = fresh(); newest = slot - STEP; }
for (; newest < slot; newest += STEP) { h.temp.push(null); h.hum.push(null); h.relay.push(null); }

const i = h.hum.length - 1;
h.temp[i] = num(d.temp, 1);
h.hum[i] = num(d.humidity, 0);
h.relay[i] = d.relay === 1 ? 1 : d.relay === 0 ? 0 : null;

const drop = h.hum.length - SLOTS;
if (drop > 0) {
    h.temp.splice(0, drop); h.hum.splice(0, drop); h.relay.splice(0, drop);
    h.t0 += drop * STEP;
}
flow.set('dehum_hist', h);

msg.topic = 'dehum/history';
msg.retain = false;
msg.payload = JSON.stringify({ v: 1, t0: h.t0, interval_s: STEP, hum: h.hum, temp: h.temp, relay: h.relay });
return msg;
