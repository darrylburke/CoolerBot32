package ai.northtrail.cooler.model

/** The trend's time spans, matching the wall panel's buttons; it opens on [DEFAULT]. */
enum class TrendZoom(val spanS: Long, val label: String) {
    H1(3_600, "1h"),
    H3(10_800, "3h"),
    H6(21_600, "6h"),
    H12(43_200, "12h");

    companion object {
        val DEFAULT = H3
    }
}

/** The time axis of the trend: the chosen span, ending now. */
data class TrendWindow(val fromEpochS: Long, val toEpochS: Long, val label: String) {
    companion object {
        fun of(zoom: TrendZoom, nowS: Long): TrendWindow =
            TrendWindow(nowS - zoom.spanS, nowS, "last ${zoom.spanS / 3_600} h")
    }
}
