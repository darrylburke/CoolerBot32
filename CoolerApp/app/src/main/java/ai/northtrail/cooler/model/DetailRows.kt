package ai.northtrail.cooler.model

import java.util.Locale
import kotlin.math.roundToLong

/** The Detail tab's read-only table, label to value. */
object DetailRows {
    fun of(s: CoolerState?, health: Health): List<Pair<String, String>> {
        val link = "Link" to health.label
        if (s == null) return listOf(link)
        return listOf(
            link,
            "Mode" to StatusText.modeLabel(s),
            "Override source" to (s.overrideSrc ?: "--"),
            "State" to StatusText.stateValue(s),
            "Relay" to if (s.relay) "Closed (cooling)" else "Open",
            "Cooling call" to if (s.coolCall) "Yes" else "No",
            "Run" to StatusText.dur(s.runS),
            "Off" to StatusText.dur(s.offS),
            "Hold" to StatusText.dur(s.holdS),
            "Compressor" to StatusText.compressor(s),
            "Coil" to "${StatusText.temp(s.finTemp)} °C",
            "Fin resistance" to (s.finOhms?.let { "${it.roundToLong()} Ω" } ?: "--"),
            "Fin slope" to (s.finSlope?.let { String.format(Locale.US, "%.2f °C/min", it) } ?: "--"),
            "Box sensor" to if (s.shtFault) "Fault" else "OK",
            "Fin sensor" to if (s.finFault) "Fault" else "OK",
            "AC response" to if (s.noResponse) "Not responding" else "OK",
            "Uptime" to (s.uptimeS?.let(StatusText::dur) ?: "--"),
        )
    }
}
