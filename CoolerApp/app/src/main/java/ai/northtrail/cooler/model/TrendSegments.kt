package ai.northtrail.cooler.model

/**
 * Where the trend line must break: a gap between two samples that is both
 * long and far wider than the spacing either side of it is missing data
 * (a controller outage in Node-RED's minute history), not a slow sample rate
 * (the controller's own hourly history, which stays one line).
 */
object TrendSegments {
    const val MIN_GAP_S = 150L
    private const val FACTOR = 2.5

    fun split(samples: List<Sample>): List<List<Sample>> {
        if (samples.isEmpty()) return emptyList()
        val d = samples.zipWithNext { a, b -> b.epochS - a.epochS }
        val out = mutableListOf<List<Sample>>()
        var start = 0
        for (i in d.indices) {
            val around = listOfNotNull(d.getOrNull(i - 1), d.getOrNull(i + 1)).minOrNull() ?: continue
            if (d[i] > MIN_GAP_S && d[i] > FACTOR * around) {
                out += samples.subList(start, i + 1)
                start = i + 1
            }
        }
        out += samples.subList(start, samples.size)
        return out
    }
}
