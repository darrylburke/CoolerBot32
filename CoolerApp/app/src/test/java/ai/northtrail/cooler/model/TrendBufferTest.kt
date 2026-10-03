package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TrendBufferTest {
    private val last = 1_790_640_000L
    private val history = CoolerState(histIntervalS = 3600, histLastTs = last, tempHist = listOf(21.0, 20.0, 19.0))

    @Test
    fun seedDatesSamplesBackFromTheLastTimestamp() {
        val b = TrendBuffer().seeded(history)
        assertEquals(
            listOf(Sample(last - 7200, 21.0, null), Sample(last - 3600, 20.0, null), Sample(last, 19.0, null)),
            b.all,
        )
    }

    @Test
    fun seedSkippedWhenClockUnsynced() {
        assertTrue(TrendBuffer().seeded(history.copy(histLastTs = 0)).all.isEmpty())
        assertTrue(TrendBuffer().seeded(history.copy(histLastTs = 12_345)).all.isEmpty())
        assertTrue(TrendBuffer().seeded(history.copy(histLastTs = null)).all.isEmpty())
    }

    @Test
    fun seedSkippedWithoutAnIntervalOrHistory() {
        assertTrue(TrendBuffer().seeded(history.copy(histIntervalS = null)).all.isEmpty())
        assertTrue(TrendBuffer().seeded(history.copy(histIntervalS = 0)).all.isEmpty())
        assertTrue(TrendBuffer().seeded(history.copy(tempHist = emptyList())).all.isEmpty())
    }

    @Test
    fun reseedingDoesNotDuplicate() {
        assertEquals(3, TrendBuffer().seeded(history).seeded(history).all.size)
    }

    @Test
    fun liveSamplesCloserThanTheMinimumGapAreDropped() {
        val b = TrendBuffer().appended(1_000, 4.0, true).appended(1_005, 4.1, true).appended(1_010, 4.2, false)
        assertEquals(listOf(Sample(1_000, 4.0, true), Sample(1_010, 4.2, false)), b.all)
    }

    @Test
    fun missingTemperatureIsNotASample() {
        assertTrue(TrendBuffer().appended(1_000, null, false).all.isEmpty())
    }

    @Test
    fun samplesOlderThanTheNewestAreDropped() {
        val b = TrendBuffer().appended(2_000, 4.0, false).appended(1_990, 5.0, false)
        assertEquals(listOf(Sample(2_000, 4.0, false)), b.all)
    }

    @Test
    fun capacityKeepsTheNewest() {
        var b = TrendBuffer()
        repeat(TrendBuffer.CAPACITY + 5) { i -> b = b.appended(10_000L + i * 10, 4.0, false) }
        assertEquals(TrendBuffer.CAPACITY, b.all.size)
        assertEquals(10_000L + 5 * 10, b.all.first().epochS)
    }

    @Test
    fun samplesSinceFiltersByTime() {
        val b = TrendBuffer().appended(1_000, 4.0, false).appended(2_000, 5.0, false)
        assertEquals(listOf(Sample(2_000, 5.0, false)), b.samples(sinceS = 1_500))
    }

    @Test
    fun historyReplacesItsSpanAndKeepsTheRest() {
        val b = TrendBuffer()
            .appended(1_000, 1.0, false)
            .appended(1_100, 2.0, false)
            .appended(1_500, 9.0, true)
        val w = HistoryWindow(1_050, 1_400, listOf(Sample(1_080, 3.0, true), Sample(1_140, 4.0, null)))
        assertEquals(
            listOf(Sample(1_000, 1.0, false), Sample(1_080, 3.0, true), Sample(1_140, 4.0, null), Sample(1_500, 9.0, true)),
            b.withHistory(w).all,
        )
    }

    @Test
    fun historyFillsAnEmptyBuffer() {
        val w = HistoryWindow(1_000, 1_120, listOf(Sample(1_000, 3.0, false), Sample(1_060, 3.1, true)))
        assertEquals(w.samples, TrendBuffer().withHistory(w).all)
    }

    @Test
    fun liveSamplesAfterTheHistoryStillAppend() {
        val w = HistoryWindow(1_000, 1_060, listOf(Sample(1_000, 3.0, false)))
        assertEquals(2, TrendBuffer().withHistory(w).appended(1_030, 3.2, true).all.size)
    }
}
