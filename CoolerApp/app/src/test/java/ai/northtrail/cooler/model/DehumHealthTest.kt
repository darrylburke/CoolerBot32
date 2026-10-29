package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class DehumHealthTest {
    private val up = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = 0)
    private val s = DehumState(mode = DehumMode.AUTO)

    @Test
    fun refusedDataIsNoAccessEvenWhileConnected() {
        assertEquals(DehumView.NO_ACCESS, DehumHealth.evaluate(up.copy(refused = setOf("dehum/data")), true, s, 0, 0))
    }

    @Test
    fun theLadder() {
        assertEquals(DehumView.BROKER_DOWN, DehumHealth.evaluate(LinkStatus(), true, s, 0, 0))
        assertEquals(DehumView.OFFLINE, DehumHealth.evaluate(up, false, s, 0, 0))
        assertEquals(DehumView.WAITING, DehumHealth.evaluate(up, true, null, null, 0))
        assertEquals(DehumView.ONLINE, DehumHealth.evaluate(up, true, s, 10_000, 100_000))
        assertEquals(DehumView.SILENT, DehumHealth.evaluate(up, true, s, 10_000, 100_001))
    }

    @Test
    fun silenceCountsFromConnectingWhenOnlyRetainedDataWasSeen() {
        val later = up.copy(connectedAtMillis = 50_000)
        assertEquals(DehumView.ONLINE, DehumHealth.evaluate(later, null, s, null, 140_000))
        assertEquals(DehumView.SILENT, DehumHealth.evaluate(later, null, s, null, 140_001))
    }
}
