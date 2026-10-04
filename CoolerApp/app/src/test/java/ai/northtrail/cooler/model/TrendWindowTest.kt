package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class TrendWindowTest {
    private val now = 1_800_000_000L

    @Test
    fun eachZoomEndsNowAndSpansItsHours() {
        for (z in TrendZoom.entries) {
            val w = TrendWindow.of(z, now)
            assertEquals(now - z.spanS, w.fromEpochS)
            assertEquals(now, w.toEpochS)
        }
    }

    @Test
    fun zoomsMatchThePanelAndOpenOnThreeHours() {
        assertEquals(listOf("1h", "3h", "6h", "12h"), TrendZoom.entries.map { it.label })
        assertEquals(listOf(3_600L, 10_800L, 21_600L, 43_200L), TrendZoom.entries.map { it.spanS })
        assertEquals(TrendZoom.H3, TrendZoom.DEFAULT)
        assertEquals("last 3 h", TrendWindow.of(TrendZoom.H3, now).label)
        assertEquals("last 1 h", TrendWindow.of(TrendZoom.H1, now).label)
    }
}
