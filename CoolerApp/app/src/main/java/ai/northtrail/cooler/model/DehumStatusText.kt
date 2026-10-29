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

    /** AUTO only: what the Cooler says and whether the room is humid. */
    fun auto(s: DehumState?): String {
        if (s?.mode != DehumMode.AUTO) return ""
        val ac = when (s.want) { 1 -> "AC resting"; 0 -> "AC cooling"; else -> "no Cooler signal" }
        return "$ac · ${if (s.humid) "humid" else "dry"}"
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
