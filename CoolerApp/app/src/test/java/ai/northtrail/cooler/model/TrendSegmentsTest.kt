package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class TrendSegmentsTest {
    private fun at(vararg t: Long) = t.map { Sample(it, 4.0, false) }

    @Test
    fun anOutageInMinuteHistorySplitsTheLine() {
        val s = at(0, 60, 120, 10_920, 10_980, 11_040)
        assertEquals(listOf(at(0, 60, 120), at(10_920, 10_980, 11_040)), TrendSegments.split(s))
    }

    @Test
    fun theControllersHourlySeedStaysOneLine() {
        val s = at(0, 3_600, 7_200, 10_800)
        assertEquals(listOf(s), TrendSegments.split(s))
    }

    @Test
    fun shortGapsNeverSplit() {
        val s = at(0, 10, 20, 140, 150)
        assertEquals(listOf(s), TrendSegments.split(s))
    }

    @Test
    fun seedThenLiveIsTwoSegments() {
        val s = at(0, 3_600, 7_200, 9_000, 9_010, 9_020)
        assertEquals(listOf(at(0, 3_600, 7_200), at(9_000, 9_010, 9_020)), TrendSegments.split(s))
    }

    @Test
    fun aLonePointIsItsOwnSegment() {
        val s = at(0, 60, 120, 5_000, 9_000, 9_060)
        assertEquals(listOf(at(0, 60, 120), at(5_000), at(9_000, 9_060)), TrendSegments.split(s))
    }

    @Test
    fun emptyAndSingle() {
        assertEquals(emptyList<List<Sample>>(), TrendSegments.split(emptyList()))
        assertEquals(listOf(at(5)), TrendSegments.split(at(5)))
    }
}
