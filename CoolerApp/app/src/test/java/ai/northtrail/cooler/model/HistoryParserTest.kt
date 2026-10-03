package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class HistoryParserTest {
    private val t0 = 1_790_700_000L
    private val now = t0 + 180

    private fun body(
        v: Int = 1,
        t0: Long = this.t0,
        interval: Int = 60,
        temp: String = "4.2,null,4.4",
        hum: String = "80,81,null",
        relay: String = "0,1,1",
    ) = """{"v":$v,"t0":$t0,"interval_s":$interval,"temp":[$temp],"hum":[$hum],"relay":[$relay]}"""

    @Test
    fun parsesSlotsAndSkipsMissingTemperatures() {
        val w = HistoryParser.parse(body(), now)!!
        assertEquals(t0, w.fromS)
        assertEquals(t0 + 180, w.toS)
        assertEquals(listOf(Sample(t0, 4.2, false), Sample(t0 + 120, 4.4, true)), w.samples)
    }

    @Test
    fun nullRelayIsUnknown() {
        val w = HistoryParser.parse(body(temp = "4.2", hum = "80", relay = "null"), now)!!
        assertEquals(listOf(Sample(t0, 4.2, null)), w.samples)
    }

    @Test
    fun rejectsWrongVersionOrInterval() {
        assertNull(HistoryParser.parse(body(v = 2), now))
        assertNull(HistoryParser.parse(body(interval = 30), now))
    }

    @Test
    fun rejectsUnequalEmptyOrOversizedArrays() {
        assertNull(HistoryParser.parse(body(hum = "80"), now))
        assertNull(HistoryParser.parse(body(temp = "", hum = "", relay = ""), now))
        val big = List(1441) { "4" }.joinToString(",")
        assertNull(HistoryParser.parse(body(temp = big, hum = big, relay = big), t0 + 1441 * 60))
    }

    @Test
    fun rejectsMissingFieldsAndJunk() {
        assertNull(HistoryParser.parse("""{"v":1,"t0":$t0,"interval_s":60,"temp":[4]}""", now))
        assertNull(HistoryParser.parse("not json", now))
        assertNull(HistoryParser.parse("[]", now))
    }

    @Test
    fun rejectsAnUnsetControllerClockAndTheFuture() {
        assertNull(HistoryParser.parse(body(t0 = 12_345), now))
        assertNull(HistoryParser.parse(body(), t0 + 180 - 121))
        assertNotNull(HistoryParser.parse(body(), t0 + 180 - 120))
    }

    @Test
    fun futureCheckSkippedWhileOwnClockUnset() {
        assertNotNull(HistoryParser.parse(body(), 0))
    }
}
