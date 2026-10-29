package ai.northtrail.cooler.model

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.longOrNull

/** `<base>/history` from Node-RED: [samples] cover `[fromS, toS)`; slots with no temperature are left out. */
data class HistoryWindow(val fromS: Long, val toS: Long, val samples: List<Sample>)

object HistoryParser {
    const val VERSION = 1
    private const val INTERVAL_S = 60L
    private const val MAX_SLOTS = 1440
    private const val MAX_AHEAD_S = 120L

    /** Null for anything that is not a sound v1 window. [nowS] is this phone's clock. */
    fun parse(payload: String, nowS: Long): HistoryWindow? =
        window(payload, nowS, listOf("temp", "hum", "relay")) { epochS, (temp, hum, relay), i ->
            val t = temp[i].number() ?: return@window null
            Sample(epochS, t, relayAt(relay, i), hum[i].number())
        }

    /**
     * A v1 window of equal-length [arrays] (one slot per minute from `t0`); [sample]
     * turns slot i into a [Sample] or null to leave it out. Shared with [DehumHistoryParser].
     */
    internal fun window(
        payload: String,
        nowS: Long,
        arrays: List<String>,
        sample: (epochS: Long, arrays: List<JsonArray>, i: Int) -> Sample?,
    ): HistoryWindow? {
        val o = runCatching { Json.parseToJsonElement(payload) }.getOrNull() as? JsonObject ?: return null
        if (o.prim("v")?.intOrNull != VERSION || o.prim("interval_s")?.longOrNull != INTERVAL_S) return null
        val t0 = o.prim("t0")?.longOrNull ?: return null
        if (t0 < TrendBuffer.MIN_VALID_EPOCH) return null
        val arr = arrays.map { o[it] as? JsonArray ?: return null }
        val n = arr.first().size
        if (n == 0 || n > MAX_SLOTS || arr.any { it.size != n }) return null
        val toS = t0 + n * INTERVAL_S
        // An unset phone clock cannot judge "the future".
        if (nowS >= TrendBuffer.MIN_VALID_EPOCH && toS > nowS + MAX_AHEAD_S) return null
        val samples = (0 until n).mapNotNull { i -> sample(t0 + i * INTERVAL_S, arr, i) }
        return HistoryWindow(t0, toS, samples)
    }

    internal fun relayAt(relay: JsonArray, i: Int): Boolean? =
        when (relay[i].number()?.toInt()) { 1 -> true; 0 -> false; else -> null }

    private fun JsonObject.prim(key: String): JsonPrimitive? =
        (this[key] as? JsonPrimitive)?.takeUnless { it is JsonNull || it.isString }

    internal fun JsonElement.number(): Double? =
        (this as? JsonPrimitive)?.takeUnless { it is JsonNull || it.isString }?.doubleOrNull
}
