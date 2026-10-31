package ai.northtrail.cooler.model

/** The Humidity page's status words. */
object DehumStatusText {
    fun mode(s: DehumState?): String {
        val m = s?.mode ?: return "--"
        return when (m) {
            DehumMode.TIMER -> "TIMER ${left(s.remainS)}"
            DehumMode.CYCLE -> "CYCLE ${if (s.phaseOn == false) "rest" else "run"} ${left(s.remainS)}"
            else -> m.label
        }
    }

    /** AUTO only: which rules hold now. */
    fun auto(s: DehumState?): String {
        if (s?.mode != DehumMode.AUTO) return ""
        val met = s.ruleMatch.withIndex().filter { it.value }.map { it.index + 1 }
        return if (met.isEmpty()) "no rule met" else "rules met: ${met.joinToString(", ")}"
    }

    /** Both relays: "Relay 1 on · Relay 2 off". */
    fun relays(s: DehumState?): String {
        if (s == null) return ""
        fun w(on: Boolean) = if (on) "on" else "off"
        return "Relay 1 ${w(s.relay)} · Relay 2 ${w(s.relay2)}"
    }

    /** Minutes rounded up; hours once there is an hour or more. */
    private fun left(remainS: Long): String {
        val min = (remainS + 59) / 60
        val h = min / 60
        val m = min % 60
        return when {
            h == 0L -> "$m m left"
            m == 0L -> "$h h left"
            else -> "$h h $m m left"
        }
    }
}
