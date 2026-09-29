package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class TrendWindowTest {
    private val now = 1_800_000_000L
    private fun s(t: Long) = Sample(t, 4.0, null)

    @Test
    fun spansOnlyTheHistoryWeHave() {
        val w = TrendWindow.of(listOf(s(now - 3 * 3_600), s(now)), now)
        assertEquals(now - 3 * 3_600, w.fromEpochS)
        assertEquals(now, w.toEpochS)
        assertEquals("last 3 h", w.label)
    }

    @Test
    fun neverWiderThan24Hours() {
        val w = TrendWindow.of(listOf(s(now - 30 * 3_600), s(now)), now)
        assertEquals(now - 24 * 3_600, w.fromEpochS)
        assertEquals("last 24 h", w.label)
    }

    @Test
    fun closeSamplesKeepAMinimumSpan() {
        val w = TrendWindow.of(listOf(s(now - 20), s(now)), now)
        assertEquals(now - 600, w.fromEpochS)
        assertEquals("last 10 min", w.label)
    }

    @Test
    fun minutesAndNoSamples() {
        assertEquals("last 45 min", TrendWindow.of(listOf(s(now - 45 * 60), s(now)), now).label)
        assertEquals("last 10 min", TrendWindow.of(emptyList(), now).label)
    }
}
