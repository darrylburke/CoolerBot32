package ai.northtrail.cooler.model

import java.util.Locale

data class Chip(val text: String, val alert: Boolean)

/**
 * The same wording as the CoolerPanel (shared/model/status_text.cpp). Change one,
 * change both: the app and the wall panel must describe the cooler identically.
 */
object StatusText {
    /** "45s" / "12m" / "1h 05m". */
    fun dur(secs: Long): String = when {
        secs < 60 -> "${secs}s"
        secs < 3_600 -> "${secs / 60}m"
        else -> String.format(Locale.US, "%dh %02dm", secs / 3_600, (secs % 3_600) / 60)
    }

    fun temp(c: Double?): String = if (c == null) "--" else String.format(Locale.US, "%.1f", c)

    fun modeLabel(s: CoolerState?): String = when (val m = s?.mode) {
        null, "" -> "--"
        "normal" -> "NORMAL"
        "override" -> "OVERRIDE"
        "fin-proxy" -> "FIN PROXY"
        "override-proxy" -> "OVR + FIN PROXY"
        "blind" -> "BLIND TIMER"
        else -> m
    }

    fun stateValue(s: CoolerState?): String {
        if (s == null) return "--"
        return when (val st = s.state) {
            null, "" -> "--"
            // Inside the minimum run the relay is held closed: show the time left.
            "cooling" -> if (s.holdS > 0) "Cooling • min run ${dur(s.holdS)}" else "Cooling ${dur(s.runS)}"
            "defrost" -> "Defrost ${dur(s.offS)}"
            "wait" -> "Waiting ${dur(s.holdS)}"
            "rest" -> if (s.holdS > 0) "Resting ${dur(s.holdS)}" else "Resting"
            // Relay open with nothing needed: the AC's continuous fan still moves box air.
            "idle" -> "Fan only ${dur(s.offS)}"
            else -> st
        }
    }

    fun stateKey(s: CoolerState?): String {
        val m = modeLabel(s)
        if (s == null || s.state != "rest") return m
        // A rest is either the timed backup duty (fin sensor failed) or the
        // fin-proxy settle before the coil can be read as box temperature.
        val why = when {
            s.finFault -> "BACKUP DUTY"
            s.mode?.contains("proxy") == true -> "SETTLE"
            else -> null
        }
        return if (why == null) m else "$m - $why"
    }

    fun compressor(s: CoolerState?): String {
        if (s == null) return "--"
        val c = s.compressor ?: return "--"
        return when {
            c == 1 -> "Running"
            s.relay -> "Starting"
            else -> "Stopped"
        }
    }

    fun finCal(s: CoolerState?): String = when {
        s == null -> "--"
        s.calActive -> String.format(Locale.US, "calibrating %d pts %.1fC", s.calPoints, s.calSpan)
        s.finCal -> "fin cal ok"
        else -> "fin uncalibrated"
    }

    fun switchChip(s: CoolerState?): Chip = when (s?.overrideSrc) {
        null, "" -> Chip("--", false)
        "switch", "both" -> Chip("SWITCH ON", true)
        "link" -> Chip("LINK LOST", true)
        else -> Chip("SWITCH OFF", false)
    }

    /** The switch chip first, then one chip per active condition. */
    fun chips(s: CoolerState?): List<Chip> {
        if (s == null) return listOf(switchChip(null))
        return buildList {
            add(switchChip(s))
            if (s.defrost) add(Chip("DEFROST", true))
            if (s.shtFault) add(Chip("BOX SENSOR FAULT", true))
            if (s.finFault) add(Chip("FIN SENSOR FAULT", true))
            if (s.noResponse) add(Chip("AC NOT RESPONDING", true))
            if (s.calActive) add(Chip("CALIBRATING", false))
        }
    }

    /** "set 12 ±2 · on >14 off <10". */
    fun setpointLine(s: CoolerState?): String {
        if (s == null) return "--"
        val set = s.settings["coolerset"] ?: return "--"
        val range = s.settings["range"] ?: return "--"
        return "set $set ±$range · on >${set + range} off <${set - range}"
    }
}
