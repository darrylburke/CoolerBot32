package ai.northtrail.cooler.model

/** Mirrors the dehumidifier's clamp_settings() (dehumidifier/dehum_logic.h). */
object DehumBounds {
    /** The four plain settings, in screen order. */
    val ALL: List<Bound> = listOf(
        Bound("timer_min", "Timer", "min", 1, 1440, 15, Widget.STEPPER, Group.DEHUM),
        Bound("cycle_on", "Cycle run", "min", 1, 240, 5, Widget.STEPPER, Group.DEHUM),
        Bound("cycle_off", "Cycle rest", "min", 1, 240, 5, Widget.STEPPER, Group.DEHUM),
        Bound("min_run", "Min run", "min", 0, 60, 1, Widget.STEPPER, Group.DEHUM),
    )

    /** AUTO's rules (spec 2026-10-31): each is six integer settings `rN_<field>`. */
    const val RULE_COUNT = 4
    val RULE_FIELDS: List<String> = listOf("en", "sensor", "cmp", "value", "relay", "action")
    const val SENSOR_TEMP = 0
    const val SENSOR_HUM = 1
    val TEMP_RANGE = -20..50
    val HUM_RANGE = 0..100

    fun ruleKey(n: Int, field: String): String = "r${n}_$field"

    val RULE_KEYS: List<String> = (1..RULE_COUNT).flatMap { n -> RULE_FIELDS.map { ruleKey(n, it) } }

    /** Every integer the firmware accepts on `dehum/cmd` and echoes in `dehum/data`. */
    val KEYS: List<String> = ALL.map { it.key } + RULE_KEYS

    private val RULE_KEY = Regex("""r([1-4])_(en|sensor|cmp|value|relay|action)""")

    /** (rule number, field) for a rule key, else null. */
    fun parseRuleKey(key: String): Pair<Int, String>? =
        RULE_KEY.matchEntire(key)?.let { it.groupValues[1].toInt() to it.groupValues[2] }

    fun find(key: String): Bound? = ALL.firstOrNull { it.key == key }

    /**
     * The legal values now. A cycle's run can't be shorter than the minimum run; a rule's
     * threshold range follows its sensor. [valueOf] reads the current value of another key.
     */
    fun range(key: String, valueOf: (String) -> Int?): IntRange? {
        find(key)?.let { b ->
            val lo = if (key == "cycle_on") maxOf(b.lo, valueOf("min_run") ?: 0) else b.lo
            return lo..b.hi
        }
        val (n, field) = parseRuleKey(key) ?: return null
        return when (field) {
            "relay" -> 1..2
            "value" -> if (valueOf(ruleKey(n, "sensor")) == SENSOR_TEMP) TEMP_RANGE else HUM_RANGE
            else -> 0..1
        }
    }

    fun step(key: String): Int = find(key)?.step ?: 1

    fun unit(key: String, valueOf: (String) -> Int?): String {
        find(key)?.let { return it.unit }
        val (n, _) = parseRuleKey(key) ?: return ""
        return if (valueOf(ruleKey(n, "sensor")) == SENSOR_TEMP) "°C" else "%RH"
    }

    fun format(key: String, v: Int?, valueOf: (String) -> Int? = { null }): String {
        if (v == null) return "--"
        val unit = unit(key, valueOf)
        return if (unit.isEmpty()) v.toString() else "$v $unit"
    }

    /** A name for messages ("Not applied: Rule 2 threshold"). */
    fun label(key: String): String {
        find(key)?.let { return it.label }
        val (n, field) = parseRuleKey(key) ?: return key
        val what = when (field) {
            "en" -> "enabled"; "sensor" -> "sensor"; "cmp" -> "above/below"
            "value" -> "threshold"; "relay" -> "relay"; else -> "on/off"
        }
        return "Rule $n $what"
    }
}
