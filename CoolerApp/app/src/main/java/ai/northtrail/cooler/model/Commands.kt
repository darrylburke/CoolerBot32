package ai.northtrail.cooler.model

import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put

data class Publish(val topic: String, val payload: String)

/** Payloads for `<base>/cmd`. The controller reads each key only if it is an integer. */
class Commands(private val topics: Topics) {
    fun setting(key: String, value: Int): Publish {
        require(Bounds.find(key) != null) { "unknown setting $key" }
        return Publish(topics.cmd, buildJsonObject { put(key, value) }.toString())
    }

    /** 1 starts a fin calibration run (ignored while one is active); 0 aborts it. */
    fun calibrate(start: Boolean): Publish =
        Publish(topics.cmd, buildJsonObject { put("calibrate", if (start) 1 else 0) }.toString())

    fun resetFinCal(): Publish = Publish(topics.cmd, buildJsonObject { put("fincal_reset", 1) }.toString())
}
