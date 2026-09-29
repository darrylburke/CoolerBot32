package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BrokerConfigTest {
    private val good = BrokerConfig(host = "mqtt.example.com", username = "app-user", password = "secret")

    @Test
    fun defaultsLeaveTheHostForSetup() {
        val c = BrokerConfig()
        assertEquals("", c.host)
        assertEquals(8883, c.port)
        assertEquals("cooler", c.base)
    }

    @Test
    fun usableOnceCredentialsAreFilledIn() {
        assertTrue(good.isUsable)
        assertTrue(good.problems.isEmpty())
    }

    @Test
    fun eachMissingFieldIsReported() {
        assertEquals(listOf("Host is required"), good.copy(host = " ").problems)
        assertEquals(listOf("Username is required"), good.copy(username = "").problems)
        assertEquals(listOf("Password is required"), good.copy(password = "").problems)
        assertFalse(good.copy(port = 0).isUsable)
        assertFalse(good.copy(port = 70000).isUsable)
    }

    @Test
    fun topicBaseMustBeAPlainTopic() {
        for (bad in listOf("", "/cooler", "cooler/#", "cooler/+")) {
            assertEquals(bad, listOf("Topic base must be a plain topic, like cooler"), good.copy(base = bad).problems)
        }
        assertTrue(good.copy(base = "site/cooler").isUsable)
    }
}
