package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DehumHistoryParserTest {
    private val t0 = 1_790_700_000L
    private val now = t0 + 180

    private fun body(v: Int = 1, hum: String = "55,null,57", temp: String = "23.1,23.2,23.3", relay: String = "1,0,null", start: Long = t0) =
        """{"v":$v,"t0":$start,"interval_s":60,"hum":[$hum],"temp":[$temp],"relay":[$relay]}"""

    @Test
    fun temperatureIsTheValueAndHumidityRidesAlong() {
        val w = DehumHistoryParser.parse(body(), now)!!
        assertEquals(t0, w.fromS)
        assertEquals(t0 + 180, w.toS)
        assertEquals(
            listOf(Sample(t0, 23.1, true, 55.0), Sample(t0 + 60, 23.2, false, null), Sample(t0 + 120, 23.3, null, 57.0)),
            w.samples,
        )
    }

    @Test
    fun slotsWithoutTemperatureAreLeftOut() {
        val w = DehumHistoryParser.parse(body(temp = "23.1,null,23.3"), now)!!
        assertEquals(listOf(t0, t0 + 120), w.samples.map { it.epochS })
    }

    @Test
    fun rejectsBadWindows() {
        assertNull(DehumHistoryParser.parse(body(hum = "55,56"), now))
        assertNull(DehumHistoryParser.parse(body(v = 2), now))
        val big = List(1441) { "5" }.joinToString(",")
        assertNull(DehumHistoryParser.parse(body(hum = big, temp = big, relay = big), now + 100_000))
        assertNull(DehumHistoryParser.parse(body(start = now + 600), now))
        assertNull(DehumHistoryParser.parse("junk", now))
    }
}
