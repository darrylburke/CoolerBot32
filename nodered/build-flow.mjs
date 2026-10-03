// Regenerates cooler-history.json from history.js. Run: node nodered/build-flow.mjs
// The broker node carries no credentials: set the login after importing.
import { readFileSync, writeFileSync } from 'node:fs';

const here = new URL('.', import.meta.url);
const func = readFileSync(new URL('history.js', here), 'utf8');
const tab = 'c001e70000000001', broker = 'c001e70000000002', fn = 'c001e70000000003', out = 'c001e70000000004';

const mqttIn = (id, topic, datatype, y) => ({
  id, type: 'mqtt in', z: tab, name: topic, topic, qos: '1', datatype, broker,
  nl: false, rap: false, rh: 0, inputs: 0, x: 170, y, wires: [[fn]],
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
