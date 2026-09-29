package ai.northtrail.cooler.model

/** One point on the trend. [relay] is null for samples seeded from the controller's history. */
data class Sample(val epochS: Long, val tempC: Double, val relay: Boolean?)

/**
 * Box temperature over time: the controller's short history (seed) plus live
 * `/data` received while the app is open. Immutable.
 */
class TrendBuffer private constructor(val all: List<Sample>) {
    constructor() : this(emptyList())

    fun seeded(state: CoolerState): TrendBuffer {
        val last = state.histLastTs ?: return this
        val interval = state.histIntervalS ?: return this
        // Before SNTP sync the controller stamps 0 (1970): no usable dates.
        if (last < MIN_VALID_EPOCH || interval <= 0 || state.tempHist.isEmpty()) return this
        val n = state.tempHist.size
        val seed = state.tempHist.mapIndexed { i, t -> Sample(last - (n - 1 - i).toLong() * interval, t, null) }
        return merged(seed)
    }

    fun appended(epochS: Long, tempC: Double?, relay: Boolean): TrendBuffer {
        if (tempC == null) return this
        val newest = all.lastOrNull()?.epochS
        // /data also publishes on every change; keep bursts from over-sampling.
        if (newest != null && epochS - newest < MIN_GAP_S) return this
        return merged(listOf(Sample(epochS, tempC, relay)))
    }

    fun samples(sinceS: Long): List<Sample> = all.filter { it.epochS >= sinceS }

    private fun merged(extra: List<Sample>): TrendBuffer {
        val byTime = LinkedHashMap<Long, Sample>()
        for (s in all + extra) byTime[s.epochS] = s
        return TrendBuffer(byTime.values.sortedBy { it.epochS }.takeLast(CAPACITY))
    }

    companion object {
        /** 24 h at the 10 s minimum spacing. */
        const val CAPACITY = 8640
        const val MIN_GAP_S = 10L
        const val MIN_VALID_EPOCH = 1_600_000_000L
    }
}
