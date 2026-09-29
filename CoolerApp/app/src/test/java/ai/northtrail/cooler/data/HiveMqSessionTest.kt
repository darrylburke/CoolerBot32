package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.Topics
import com.hivemq.client.mqtt.MqttClientState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.net.ServerSocket
import java.util.concurrent.CopyOnWriteArrayList

class HiveMqSessionTest {
    private val links = CopyOnWriteArrayList<LinkState>()
    private val messages = CopyOnWriteArrayList<IncomingMessage>()

    private fun session(port: Int) = HiveMqSession(
        host = "127.0.0.1",
        port = port,
        username = "app-user",
        password = "x".toByteArray(),
        clientId = "test-${System.nanoTime()}",
        trustManagerFactory = null,
        subscriptions = Topics().subscriptions,
        onLink = { state, _, _ -> links += state },
        onMessage = { messages += it },
    )

    @Test
    fun subscribesToExactlyTheTwoCoolerTopics() {
        FakeBroker().use { broker ->
            val s = session(broker.port)
            s.start()
            waitUntil { LinkState.CONNECTED in links }
            s.stop()
            assertTrue("links seen: $links", LinkState.CONNECTED in links)
            assertEquals(listOf("cooler/data", "cooler/availability"), broker.subscribedTopics.toList())
        }
    }

    @Test
    fun deliversMessagesWithTheirTopic() {
        FakeBroker(publishAfterSubscribe = "cooler/data" to """{"v":2}""").use { broker ->
            val s = session(broker.port)
            s.start()
            waitUntil { messages.isNotEmpty() }
            s.stop()
            assertEquals(IncomingMessage("cooler/data", """{"v":2}""", retained = false), messages.first())
        }
    }

    @Test
    fun rejectedCredentialsReportRejectedAndDoNotRetry() {
        FakeBroker(connackCode = 5).use { broker ->
            val s = session(broker.port)
            s.start()
            Thread.sleep(4_000)
            assertTrue("links seen: $links", LinkState.REJECTED in links)
            assertEquals("connection attempts", 1, broker.accepted.get())
            assertEquals(MqttClientState.DISCONNECTED, s.clientState)
        }
    }

    @Test
    fun stoppedSessionStopsReconnectingWhileTheBrokerIsUnreachable() {
        val closedPort = ServerSocket(0).use { it.localPort }
        val s = session(closedPort)
        s.start()
        Thread.sleep(1_500) // first attempt fails; an automatic reconnect is now scheduled
        s.stop()
        Thread.sleep(5_000) // long enough for that attempt to run
        assertEquals(MqttClientState.DISCONNECTED, s.clientState)
    }
}
