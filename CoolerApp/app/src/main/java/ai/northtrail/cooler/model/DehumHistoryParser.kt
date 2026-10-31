package ai.northtrail.cooler.model

import ai.northtrail.cooler.model.HistoryParser.number

/** `dehum/history` from Node-RED: like `cooler/history`, but the value is humidity; slots without one are left out. */
object DehumHistoryParser {
    fun parse(payload: String, nowS: Long): HistoryWindow? =
        // Temperature is the primary value and humidity rides along, the Cooler's shape.
        HistoryParser.window(payload, nowS, listOf("hum", "temp", "relay")) { epochS, (hum, temp, relay), i ->
            val t = temp[i].number() ?: return@window null
            Sample(epochS, t, HistoryParser.relayAt(relay, i), hum[i].number())
        }
}
