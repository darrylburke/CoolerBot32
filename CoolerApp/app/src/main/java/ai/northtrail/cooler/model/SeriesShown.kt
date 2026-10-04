package ai.northtrail.cooler.model

enum class Series { TEMP, RH }

/** Which traces the trend draws; both by default, and never neither. */
data class SeriesShown(val temp: Boolean = true, val rh: Boolean = true) {
    /** [which] flipped, or unchanged when that would hide the last trace shown. */
    fun toggled(which: Series): SeriesShown {
        val next = when (which) {
            Series.TEMP -> copy(temp = !temp)
            Series.RH -> copy(rh = !rh)
        }
        return if (next.temp || next.rh) next else this
    }
}
