package ai.northtrail.cooler.model

/**
 * Time-weighted temperature averages over the trend. The record is integrated as the
 * chart draws it -- straight lines between samples, broken where [TrendSegments]
 * breaks it -- so live samples every 10 s count no more than history every 60 s,
 * and a gap counts for nothing.
 */
object TrendAverage {
    /** The rolling line's window: about one cooling cycle of this box. */
    const val WINDOW_S = 3_600L
    /**
     * A rolling point needs the record to reach back to the start of its window
     * (or it would average part of a cycle and read high or low), and this share
     * of the window on record (a few missing minutes inside it are fine).
     */
    private const val MIN_COVER = 0.9

    /** Mean temperature over `[fromS, toS]`; null with no data there. */
    fun mean(samples: List<Sample>, fromS: Long, toS: Long): Double? {
        val segs = segments(samples)
        val cover = segs.sumOf { it.cover(fromS, toS) }
        if (cover <= 0) return null
        return segs.sumOf { it.area(toS) - it.area(fromS) } / cover
    }

    /**
     * The mean over the [WINDOW_S] before each sample at or after [fromS], as
     * (epoch s, °C) points. Pass samples from `fromS - WINDOW_S` on so the line
     * reaches the left edge.
     */
    fun rolling(samples: List<Sample>, fromS: Long): List<Pair<Long, Double>> {
        val segs = segments(samples)
        val start = segs.minOfOrNull { it.t.first() } ?: return emptyList()
        val out = ArrayList<Pair<Long, Double>>()
        for (s in samples) {
            if (s.epochS < fromS) continue
            val a = s.epochS - WINDOW_S
            if (a < start) continue
            val cover = segs.sumOf { it.cover(a, s.epochS) }
            if (cover < MIN_COVER * WINDOW_S) continue
            out += s.epochS to segs.sumOf { it.area(s.epochS) - it.area(a) } / cover
        }
        return out
    }

    /** One unbroken run of samples, with the running integral (°C·s) at each. */
    private class Seg(val t: LongArray, val v: DoubleArray) {
        private val acc = DoubleArray(t.size).also {
            for (i in 1 until t.size) it[i] = it[i - 1] + (v[i - 1] + v[i]) / 2 * (t[i] - t[i - 1])
        }

        /** Integral from the segment's start to [x], clamped to the segment. */
        fun area(x: Long): Double {
            if (x <= t.first()) return 0.0
            if (x >= t.last()) return acc.last()
            var k = t.binarySearch(x).let { if (it >= 0) it else -it - 2 }
            while (k + 1 < t.size && t[k + 1] == t[k]) k++   // duplicate times: take the last
            if (t[k] == x) return acc[k]
            val vx = v[k] + (v[k + 1] - v[k]) * (x - t[k]) / (t[k + 1] - t[k])
            return acc[k] + (v[k] + vx) / 2 * (x - t[k])
        }

        /** Seconds of `[a, b]` this segment covers. */
        fun cover(a: Long, b: Long): Long = maxOf(0L, minOf(b, t.last()) - maxOf(a, t.first()))
    }

    private fun segments(samples: List<Sample>): List<Seg> =
        TrendSegments.split(samples).filter { it.size >= 2 }.map { seg ->
            Seg(LongArray(seg.size) { seg[it].epochS }, DoubleArray(seg.size) { seg[it].tempC })
        }
}
