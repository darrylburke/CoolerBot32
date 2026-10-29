# Node-RED: 24-hour history

Two flows, same pattern: `cooler-history.json` (below) and `dehum-history.json`
(the dehumidifier, at the end).

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
   Leave "Retain as published" (MQTT v5 only) unticked on the two `mqtt in` nodes: with it,
   every live `cooler/data` arrives flagged retained and the flow ignores all of them.
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
`node nodered/build-flow.mjs && node --test nodered/test/*.mjs`, then re-import.
(`dehum-history.js` likewise; the build writes both flow files.)

## Dehumidifier history

`dehum-history.json` keeps 24 h of `dehum/data` (humidity, temperature, relay) in
one-minute slots and republishes it on `dehum/history` (not retained) after every live
`dehum/data`, as `{"v":1,"t0":…,"interval_s":60,"hum":[…],"temp":[…],"relay":[…]}`.
The app's Humidity tab merges it into its graph.

Install: Menu → Import → `dehum-history.json`. It uses the same broker config node
(same id) as the cooler flow, so Node-RED offers to keep the existing one: keep it.
Deploy. Within 30 s, `mosquitto_sub -t dehum/history -C 1` shows a message.

Add to Node-RED's login:

    topic read dehum/data
    topic read dehum/availability
    topic write dehum/history

and `topic read dehum/history` to the panel's and the app's logins.
