package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class PendingCommandsTest {
    private val sent = mapOf("coolerset" to Pending(7, sentAtMillis = 1_000))
    private val seven = CoolerState(settings = mapOf("coolerset" to 7))
    private val four = CoolerState(settings = mapOf("coolerset" to 4))

    @Test
    fun liveMatchingValueAfterSendingConfirms() {
        val r = PendingCommands.resolve(sent, seven, stateLiveAtMillis = 1_500, nowMillis = 1_500)
        assertEquals(setOf("coolerset"), r.confirmed)
        assertEquals(emptyMap<String, Pending>(), r.pending)
    }

    @Test
    fun dataFromBeforeTheSendDoesNotConfirm() {
        val r = PendingCommands.resolve(sent, seven, stateLiveAtMillis = 900, nowMillis = 1_500)
        assertEquals(sent, r.pending)
    }

    @Test
    fun retainedDataDoesNotConfirm() {
        val r = PendingCommands.resolve(sent, seven, stateLiveAtMillis = null, nowMillis = 1_500)
        assertEquals(sent, r.pending)
    }

    @Test
    fun aDifferentValueKeepsWaiting() {
        val r = PendingCommands.resolve(sent, four, stateLiveAtMillis = 1_500, nowMillis = 1_500)
        assertEquals(sent, r.pending)
        assertEquals(emptySet<String>(), r.timedOut)
    }

    @Test
    fun timesOutAtThreeSeconds() {
        assertEquals(sent, PendingCommands.resolve(sent, four, 1_500, nowMillis = 3_999).pending)
        val r = PendingCommands.resolve(sent, four, 1_500, nowMillis = 4_000)
        assertEquals(setOf("coolerset"), r.timedOut)
        assertEquals(emptyMap<String, Pending>(), r.pending)
    }
}
