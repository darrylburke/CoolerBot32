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
        assertEquals("cooler/history", t.history)
    }

    @Test
    fun subscribesToExactlyDataAndAvailability() {
        assertEquals(listOf("barn/data", "barn/availability"), Topics("barn").subscriptions)
    }

    @Test
    fun surroundingSpaceAndTrailingSlashAreDropped() {
        assertEquals("cooler/data", Topics(" cooler/ ").data)
    }

    @Test
    fun historyIsAnOptionalSubscription() {
        assertEquals(listOf("barn/history"), Topics("barn").optionalSubscriptions)
    }
}
