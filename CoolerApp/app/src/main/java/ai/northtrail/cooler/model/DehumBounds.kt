package ai.northtrail.cooler.model

/** Mirrors the dehumidifier's clamp_settings() (dehumidifier/dehum_logic.h). */
object DehumBounds {
    val KEYS: List<String> = listOf("timer_min", "cycle_on", "cycle_off", "min_run", "dehum_rh")

    val ALL: List<Bound> = listOf(
        Bound("timer_min", "Timer", "min", 1, 1440, 15, Widget.STEPPER, Group.DEHUM),
        Bound("cycle_on", "Cycle run", "min", 1, 240, 5, Widget.STEPPER, Group.DEHUM),
        Bound("cycle_off", "Cycle rest", "min", 1, 240, 5, Widget.STEPPER, Group.DEHUM),
        Bound("min_run", "Min run", "min", 0, 60, 1, Widget.STEPPER, Group.DEHUM),
        Bound("dehum_rh", "AUTO above", "%RH", 30, 95, 1, Widget.STEPPER, Group.DEHUM),
    )

    fun find(key: String): Bound? = ALL.firstOrNull { it.key == key }

    /** The lowest legal value; a cycle's run can't be shorter than the minimum run. */
    fun floor(key: String, minRun: Int): Int {
        val b = find(key) ?: return Int.MIN_VALUE
        return if (key == "cycle_on") maxOf(b.lo, minRun) else b.lo
    }

    fun hi(key: String): Int = find(key)?.hi ?: Int.MAX_VALUE

    fun format(key: String, v: Int?): String {
        if (v == null) return "--"
        val b = find(key) ?: return v.toString()
        return "$v ${b.unit}"
    }
}
