package ai.northtrail.cooler.model

import ai.northtrail.cooler.model.HistoryParser.number

/** `dehum/history` from Node-RED: like `cooler/history`, but the value is humidity; slots without one are left out. */
object DehumHistoryParser {
    fun parse(payload: String, nowS: Long): HistoryWindow? =
        HistoryParser.window(payload, nowS, listOf("hum", "temp", "relay")) { epochS, (hum, _, relay), i ->
            val h = hum[i].number() ?: return@window null
            Sample(epochS, h, HistoryParser.relayAt(relay, i))
        }
}
