package ai.northtrail.cooler.model

enum class Series { TEMP, RH, AVG }

/**
 * Which traces the trend draws; all by default. Temperature and humidity are never
 * both hidden; the rolling average is an overlay and toggles freely.
 */
data class SeriesShown(val temp: Boolean = true, val rh: Boolean = true, val avg: Boolean = true) {
    /** [which] flipped, or unchanged when that would hide the last trace shown. */
    fun toggled(which: Series): SeriesShown {
        val next = when (which) {
            Series.TEMP -> copy(temp = !temp)
            Series.RH -> copy(rh = !rh)
            Series.AVG -> copy(avg = !avg)
        }
        return if (next.temp || next.rh) next else this
    }
}
