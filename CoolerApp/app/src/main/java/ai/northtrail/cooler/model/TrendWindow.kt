package ai.northtrail.cooler.model

/** The time axis of the trend: the history we actually have, capped at 24 h. */
data class TrendWindow(val fromEpochS: Long, val toEpochS: Long, val label: String) {
    companion object {
        const val MAX_SPAN_S = 24 * 3_600L
        const val MIN_SPAN_S = 600L

        fun of(samples: List<Sample>, nowS: Long): TrendWindow {
            val first = samples.firstOrNull()?.epochS ?: nowS
            val from = minOf(maxOf(nowS - MAX_SPAN_S, first), nowS - MIN_SPAN_S)
            return TrendWindow(from, nowS, "last ${spanText(nowS - from)}")
        }

        private fun spanText(secs: Long): String = when {
            secs < 3_600 -> "${(secs + 30) / 60} min"
            else -> "${(secs + 1_800) / 3_600} h"
        }
    }
}
