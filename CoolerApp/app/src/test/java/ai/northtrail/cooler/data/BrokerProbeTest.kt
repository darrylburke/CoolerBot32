package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.net.ServerSocket

class BrokerProbeTest {
    private fun config(port: Int) =
        BrokerConfig(host = "127.0.0.1", port = port, username = "app-user", password = "x")

    private fun probe(port: Int, timeoutMs: Long = 5_000) = runBlocking {
        BrokerProbe.run(config(port), trust = null, clientId = "probe-${System.nanoTime()}", timeoutMs = timeoutMs)
    }

    @Test
    fun okWhenCoolerDataArrives() {
        FakeBroker(publishAfterSubscribe = "cooler/data" to """{"v":2}""").use {
            assertEquals(ProbeResult.Ok, probe(it.port))
        }
    }

    @Test
    fun loginRefusedOnBadCredentials() {
        FakeBroker(connackCode = 5).use {
            assertEquals(ProbeResult.LoginRefused, probe(it.port))
        }
    }

    @Test
    fun unreachableWhenNothingListens() {
        val closedPort = ServerSocket(0).use { it.localPort }
        assertEquals(ProbeResult.Unreachable, probe(closedPort))
    }

    @Test
    fun noDataWhenBrokerIsSilent() {
        FakeBroker().use {
            assertEquals(ProbeResult.NoData("cooler/data"), probe(it.port, timeoutMs = 1_500))
        }
    }

    @Test
    fun eachResultHasAMessage() {
        assertNull(ProbeResult.Ok.message())
        assertEquals("Login refused: check the username, password and broker ACL", ProbeResult.LoginRefused.message())
        assertEquals("Can't verify the broker (expired)", ProbeResult.TlsFailed("expired").message())
        assertEquals("Can't reach the broker", ProbeResult.Unreachable.message())
        assertEquals("Connected, but no cooler data on cooler/data", ProbeResult.NoData("cooler/data").message())
    }
}
