package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DehumStateParserTest {
    private val real = """{"v":1,"mode":"cycle","relay":1,"want":null,"link_lost":0,"hold_s":120,"remain_s":300,""" +
        """"phase":"on","temp":23.6,"humidity":54.5,"humid":0,"dehum_rh":65,"min_run":10,"timer_min":120,""" +
        """"cycle_on":30,"cycle_off":30,"run_s":25,"uptime_s":20}"""

    @Test
    fun parsesTheFirmwaresPayload() {
        val s = DehumStateParser.parse(real)!!
        assertEquals(DehumMode.CYCLE, s.mode)
        assertTrue(s.relay)
        assertNull(s.want)
        assertFalse(s.linkLost)
        assertEquals(120L, s.holdS)
        assertEquals(300L, s.remainS)
        assertEquals(true, s.phaseOn)
        assertEquals(54.5, s.humidity!!, 0.0)
        assertEquals(23.6, s.temp!!, 0.0)
        assertFalse(s.humid)
        assertEquals(mapOf("timer_min" to 120, "cycle_on" to 30, "cycle_off" to 30, "min_run" to 10, "dehum_rh" to 65), s.settings)
        assertEquals(25L, s.runS)
        assertEquals(20L, s.uptimeS)
    }

    @Test
    fun nullReadingsStayNull() {
        val s = DehumStateParser.parse(real.replace("\"temp\":23.6", "\"temp\":null").replace("\"humidity\":54.5", "\"humidity\":null"))!!
        assertNull(s.temp)
        assertNull(s.humidity)
    }

    @Test
    fun wantAndPhaseValues() {
        val s = DehumStateParser.parse(real.replace("\"want\":null", "\"want\":1").replace("\"phase\":\"on\"", "\"phase\":\"off\""))!!
        assertEquals(1, s.want)
        assertEquals(false, s.phaseOn)
        assertNull(DehumStateParser.parse(real.replace("\"phase\":\"on\"", "\"phase\":null"))!!.phaseOn)
    }

    @Test
    fun rejectsOtherVersionsAndJunk() {
        assertNull(DehumStateParser.parse(real.replace("\"v\":1", "\"v\":2")))
        assertNull(DehumStateParser.parse("[]"))
        assertNull(DehumStateParser.parse("not json"))
    }

    @Test
    fun unknownModeIsNull() {
        assertNull(DehumStateParser.parse(real.replace("\"cycle\"", "\"bogus\""))!!.mode)
        assertEquals(DehumMode.TIMER, DehumMode.of("timer"))
        assertNull(DehumMode.of(null))
    }
}
