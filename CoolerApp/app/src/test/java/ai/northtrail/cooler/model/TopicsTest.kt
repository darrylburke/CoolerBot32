package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class TopicsTest {
    @Test
    fun defaultBaseIsCooler() {
        val t = Topics()
        assertEquals("cooler/data", t.data)
        assertEquals("cooler/availability", t.availability)
        assertEquals("cooler/cmd", t.cmd)
    }

    @Test
    fun subscribesToExactlyDataAndAvailability() {
        assertEquals(listOf("barn/data", "barn/availability"), Topics("barn").subscriptions)
    }

    @Test
    fun surroundingSpaceAndTrailingSlashAreDropped() {
        assertEquals("cooler/data", Topics(" cooler/ ").data)
    }
}
