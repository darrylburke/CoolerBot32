package ai.northtrail.cooler.model

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.longOrNull

enum class DehumMode(val wire: String, val label: String) {
    AUTO("auto", "AUTO"), ON("on", "ON"), OFF("off", "OFF"), TIMER("timer", "TIMER"), CYCLE("cycle", "CYCLE");

    companion object {
        /** The order of the mode buttons. */
        val BUTTONS: List<DehumMode> = listOf(ON, OFF, TIMER, AUTO, CYCLE)

        fun of(wire: String?): DehumMode? = entries.firstOrNull { it.wire == wire }
    }
}

/** One `dehum/data` snapshot (RULES-dehum.md). Null readings mean the sensor gave none. */
data class DehumState(
    val mode: DehumMode? = null,
    val relay: Boolean = false,
    /** The Cooler's last want (1 = AC resting) within 90 s, else null. */
    val want: Int? = null,
    val linkLost: Boolean = false,
    val holdS: Long = 0,
    /** TIMER: time left; CYCLE: time left in this phase. */
    val remainS: Long = 0,
    /** CYCLE only: true in the run phase, false resting. */
    val phaseOn: Boolean? = null,
    val temp: Double? = null,
    val humidity: Double? = null,
    val humid: Boolean = false,
    val settings: Map<String, Int> = emptyMap(),
    val runS: Long = 0,
    val uptimeS: Long? = null,
)

object DehumStateParser {
    const val VERSION = 1

    fun parse(payload: String): DehumState? {
        val o = runCatching { Json.parseToJsonElement(payload) }.getOrNull() as? JsonObject ?: return null
        if (o.num("v")?.intOrNull != VERSION) return null
        return DehumState(
            mode = DehumMode.of(o.text("mode")),
            relay = o.num("relay")?.intOrNull == 1,
            want = o.num("want")?.intOrNull?.takeIf { it == 0 || it == 1 },
            linkLost = o.num("link_lost")?.intOrNull == 1,
            holdS = o.num("hold_s")?.longOrNull ?: 0,
            remainS = o.num("remain_s")?.longOrNull ?: 0,
            phaseOn = when (o.text("phase")) { "on" -> true; "off" -> false; else -> null },
            temp = o.num("temp")?.doubleOrNull,
            humidity = o.num("humidity")?.doubleOrNull,
            humid = o.num("humid")?.intOrNull == 1,
            settings = DehumBounds.KEYS.mapNotNull { k -> o.num(k)?.intOrNull?.let { k to it } }.toMap(),
            runS = o.num("run_s")?.longOrNull ?: 0,
            uptimeS = o.num("uptime_s")?.longOrNull,
        )
    }

    private fun JsonObject.num(key: String): JsonPrimitive? =
        (this[key] as? JsonPrimitive)?.takeUnless { it is JsonNull || it.isString }

    private fun JsonObject.text(key: String): String? =
        (this[key] as? JsonPrimitive)?.takeIf { it.isString }?.content
}
