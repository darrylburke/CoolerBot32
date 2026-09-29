package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.LinkStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ConnectGateTest {
    private val config = BrokerConfig(host = "mqtt.example.com", username = "u", password = "p")
    private val rejected = LinkStatus(LinkState.REJECTED, "bad credentials")

    @Test
    fun foregroundFollowsTheActivity() {
        val gate = ConnectGate()
        assertFalse(gate.foreground)
        gate.enterForeground()
        assertTrue(gate.foreground)
        gate.leaveForeground()
        assertFalse(gate.foreground)
    }

    @Test
    fun aFatalConfigIsNotRetriedButANewOneIs() {
        val gate = ConnectGate()
        gate.recordFatal(config, rejected)
        assertEquals(rejected, gate.fatalFor(config.copy()))
        assertNull(gate.fatalFor(config.copy(password = "other")))
        assertNull(gate.fatalFor(config.copy(base = "barn")))
    }

    @Test
    fun clearingForgetsTheFatal() {
        val gate = ConnectGate()
        gate.recordFatal(config, rejected)
        gate.clearFatal()
        assertNull(gate.fatalFor(config))
    }
}
