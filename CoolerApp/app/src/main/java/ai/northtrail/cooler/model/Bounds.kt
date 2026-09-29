package ai.northtrail.cooler.model

enum class Widget { STEPPER, PRESET }

enum class Group(val title: String) { BOX("Box"), COIL("Coil"), TIMING("Timing") }

data class Bound(
    val key: String,
    val label: String,
    val unit: String,
    val lo: Int,
    val hi: Int,
    val step: Int,
    val widget: Widget,
    val group: Group,
)

/**
 * Mirrors CoolerPanel shared/model/bounds.cpp, which mirrors the controller's own
 * clamp_settings(). dutypercent floors at 5 (not the controller's 1) so a step of 5
 * gives 5, 10, 15 ...
 */
object Bounds {
    val SAMPLE_PRESETS: List<Pair<Int, String>> =
        listOf(10 to "10 s", 60 to "1 m", 300 to "5 m", 900 to "15 m", 3600 to "1 h")

    val ALL: List<Bound> = listOf(
        Bound("coolerset", "Set point", "°C", 2, 40, 1, Widget.STEPPER, Group.BOX),
        Bound("range", "Range ±", "°C", 0, 5, 1, Widget.STEPPER, Group.BOX),
        Bound("sampleinterval", "Sample every", "s", 10, 3600, 0, Widget.PRESET, Group.BOX),
        Bound("fin_cutoff", "Ice cutoff", "°C", -5, 5, 1, Widget.STEPPER, Group.COIL),
        Bound("fin_recover", "Ice clear", "°C", -4, 10, 1, Widget.STEPPER, Group.COIL),
        Bound("settle", "Settle", "min", 2, 30, 1, Widget.STEPPER, Group.COIL),
        Bound("minofftime", "Min off", "min", 0, 30, 1, Widget.STEPPER, Group.TIMING),
        Bound("minruntime", "Min run", "s", 0, 600, 30, Widget.STEPPER, Group.TIMING),
        Bound("maxrun", "Max run", "min", 1, 60, 1, Widget.STEPPER, Group.TIMING),
        Bound("dutypercent", "Backup duty", "%", 5, 100, 5, Widget.STEPPER, Group.TIMING),
    )

    fun find(key: String): Bound? = ALL.firstOrNull { it.key == key }

    /** The lowest legal value; "Ice clear" must stay above "Ice cutoff". */
    fun floor(key: String, finCutoff: Int): Int {
        val b = find(key) ?: return Int.MIN_VALUE
        return if (key == "fin_recover") maxOf(b.lo, finCutoff + 1) else b.lo
    }

    fun clamp(key: String, value: Int, finCutoff: Int): Int {
        val b = find(key) ?: return value
        return value.coerceIn(floor(key, finCutoff), b.hi)
    }

    fun format(key: String, value: Int?): String {
        if (value == null) return "--"
        val b = find(key) ?: return value.toString()
        if (b.widget == Widget.PRESET) {
            return SAMPLE_PRESETS.firstOrNull { it.first == value }?.second ?: "$value ${b.unit}"
        }
        return "$value ${b.unit}"
    }
}
