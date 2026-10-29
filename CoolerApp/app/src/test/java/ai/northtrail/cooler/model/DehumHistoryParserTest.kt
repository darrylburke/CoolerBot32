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
    fun keepsSlotsWithHumidity() {
        val w = DehumHistoryParser.parse(body(), now)!!
        assertEquals(t0, w.fromS)
        assertEquals(t0 + 180, w.toS)
        assertEquals(listOf(Sample(t0, 55.0, true), Sample(t0 + 120, 57.0, null)), w.samples)
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
