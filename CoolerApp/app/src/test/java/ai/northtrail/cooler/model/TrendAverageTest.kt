package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TrendAverageTest {
    /** One sample every [step] s from [from] for [n] samples, temperature from [f]. */
    private fun series(from: Long, n: Int, step: Long = 60, f: (Int) -> Double) =
        (0 until n).map { Sample(from + it * step, f(it), false) }

    @Test
    fun meanOfAFlatLineIsThatLine() {
        assertEquals(4.0, TrendAverage.mean(series(0, 61) { 4.0 }, 0, 3_600)!!, 1e-9)
    }

    @Test
    fun meanIsTimeWeightedNotPerSample() {
        // 2 deg for 50 min at one sample a minute, then 8 deg for 10 min at one every 10 s:
        // per-sample that leans hard on the 8s; by time it is (2*50 + 8*10) / 60 = 3.
        val slow = series(0, 51) { 2.0 }
        val fast = series(3_000, 61, step = 10) { 8.0 }
        val m = TrendAverage.mean(slow + fast, 0, 3_600)!!
        assertEquals(3.0, m, 0.15)   // trapezoids blur the 2->8 step by half a minute
    }

    @Test
    fun meanIgnoresGapsAndHasNoValueWithoutData() {
        val s = series(0, 11) { 4.0 } + series(10_000, 11) { 6.0 }
        assertEquals(5.0, TrendAverage.mean(s, 0, 20_000)!!, 1e-9)   // the gap counts for nothing
        assertNull(TrendAverage.mean(emptyList(), 0, 3_600))
    }

    @Test
    fun rollingFlattensAnHourlySawtoothToItsMean() {
        // 3 h of a 60-min sawtooth from 0 to 6 deg, one sample a minute.
        val s = series(0, 181) { (it % 60) / 10.0 }
        val r = TrendAverage.rolling(s, fromS = 0)
        assertTrue(r.isNotEmpty())
        r.forEach { (_, v) -> assertEquals(2.95, v, 0.1) }
    }

    @Test
    fun rollingStartsOnceAFullHourIsBehindIt() {
        val s = series(0, 181) { 4.0 }
        val r = TrendAverage.rolling(s, fromS = 0)
        assertEquals(3_600L, r.first().first)   // a whole hour on record behind it, not 90% of one
        assertEquals(180 * 60L, r.last().first)
    }

    @Test
    fun rollingPausesAfterAnOutage() {
        val s = series(0, 121) { 4.0 } + series(7_200 + 3_600, 121) { 4.0 }
        val times = TrendAverage.rolling(s, fromS = 0).map { it.first }
        assertTrue(times.none { it in 7_201 until 7_200 + 3_600 + 3_000 })
    }

    @Test
    fun rollingOnlyReturnsPointsFromTheVisibleWindowOn() {
        val s = series(0, 181) { 4.0 }
        assertTrue(TrendAverage.rolling(s, fromS = 7_200).all { it.first >= 7_200 })
    }
}
