package ai.northtrail.cooler.model

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.longOrNull

/** Turns a `cooler/data` payload into a [CoolerState]; null for anything that isn't v2 JSON. */
object CoolerStateParser {
    const val VERSION = 2

    fun parse(payload: String): CoolerState? {
        val o = runCatching { Json.parseToJsonElement(payload) }.getOrNull() as? JsonObject ?: return null
        if (o.int("v") != VERSION) return null
        return CoolerState(
            temp = o.double("temp"),
            humidity = o.double("humidity"),
            finTemp = o.double("fin_temp"),
            finOhms = o.double("fin_ohms"),
            finSlope = o.double("fin_slope"),
            mode = o.text("mode"),
            overrideSrc = o.text("override_src"),
            state = o.text("state"),
            relay = o.flag("relay"),
            coolCall = o.flag("cool_call"),
            defrost = o.flag("defrost"),
            compressor = o.int("compressor"),
            shtFault = o.flag("sht_fault"),
            finFault = o.flag("fin_fault"),
            noResponse = o.flag("no_response"),
            runS = o.long("run_s") ?: 0,
            offS = o.long("off_s") ?: 0,
            holdS = o.long("hold_s") ?: 0,
            settings = SETTING_KEYS.mapNotNull { key -> o.int(key)?.let { key to it } }.toMap(),
            finCal = o.flag("fin_cal"),
            calActive = o.flag("cal_active"),
            calPoints = o.int("cal_points") ?: 0,
            calSpan = o.double("cal_span") ?: 0.0,
            histIntervalS = o.int("hist_interval_s"),
            histLastTs = o.long("hist_last_ts"),
            tempHist = o.doubles("temp_hist"),
            uptimeS = o.long("uptime_s"),
        )
    }

    private fun JsonObject.prim(key: String): JsonPrimitive? =
        (this[key] as? JsonPrimitive)?.takeUnless { it is JsonNull }

    private fun JsonObject.double(key: String): Double? = prim(key)?.doubleOrNull
    private fun JsonObject.int(key: String): Int? = prim(key)?.intOrNull
    private fun JsonObject.long(key: String): Long? = prim(key)?.longOrNull
    private fun JsonObject.flag(key: String): Boolean = int(key) == 1
    private fun JsonObject.text(key: String): String? = prim(key)?.takeIf { it.isString }?.content

    private fun JsonObject.doubles(key: String): List<Double> =
        (this[key] as? JsonArray)
            ?.mapNotNull { (it as? JsonPrimitive)?.takeUnless { p -> p is JsonNull }?.doubleOrNull }
            .orEmpty()
}
