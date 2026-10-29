package ai.northtrail.cooler.model

import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put

/** Payloads for `dehum/cmd` (never retained). The firmware reads settings only as integers. */
class DehumCommands {
    fun mode(m: DehumMode): Publish = Publish(DehumTopics.CMD, buildJsonObject { put("mode", m.wire) }.toString())

    fun setting(key: String, value: Int): Publish {
        require(key in DehumBounds.KEYS) { "unknown dehumidifier setting $key" }
        return Publish(DehumTopics.CMD, buildJsonObject { put(key, value) }.toString())
    }
}
