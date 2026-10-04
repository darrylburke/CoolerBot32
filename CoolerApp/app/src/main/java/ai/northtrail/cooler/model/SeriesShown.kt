package ai.northtrail.cooler.model

enum class Series { TEMP, RH, AVG }

/** Which traces the trend draws: all by default, and any or all can be hidden. */
data class SeriesShown(val temp: Boolean = true, val rh: Boolean = true, val avg: Boolean = true) {
    fun toggled(which: Series): SeriesShown = when (which) {
        Series.TEMP -> copy(temp = !temp)
        Series.RH -> copy(rh = !rh)
        Series.AVG -> copy(avg = !avg)
    }
}
