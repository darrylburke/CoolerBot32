package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DehumStateParserTest {
    private val real = """{"v":1,"mode":"cycle","relay":1,"relay2":0,"link_lost":0,"hold_s":120,"hold2_s":0,"remain_s":300,""" +
        """"phase":"on","temp":23.6,"humidity":54.5,"min_run":10,"timer_min":120,""" +
        """"r1_en":1,"r1_sensor":1,"r1_cmp":1,"r1_value":65,"r1_relay":1,"r1_action":1,"r1_match":0,"r2_en":0,"r2_sensor":1,"r2_cmp":0,"r2_value":55,"r2_relay":1,"r2_action":0,"r2_match":0,"r3_en":1,"r3_sensor":0,"r3_cmp":1,"r3_value":10,"r3_relay":2,"r3_action":1,"r3_match":1,"r4_en":0,"r4_sensor":0,"r4_cmp":0,"r4_value":2,"r4_relay":2,"r4_action":1,"r4_match":0,""" +
        """"cycle_on":30,"cycle_off":30,"run_s":25,"uptime_s":20}"""

    @Test
    fun parsesTheFirmwaresPayload() {
        val s = DehumStateParser.parse(real)!!
        assertEquals(DehumMode.CYCLE, s.mode)
        assertTrue(s.relay)
        assertFalse(s.relay2)
        assertFalse(s.linkLost)
        assertEquals(120L, s.holdS)
        assertEquals(300L, s.remainS)
        assertEquals(true, s.phaseOn)
        assertEquals(54.5, s.humidity!!, 0.0)
        assertEquals(23.6, s.temp!!, 0.0)
        assertEquals(listOf(false, false, true, false), s.ruleMatch)
        assertEquals(mapOf("timer_min" to 120, "cycle_on" to 30, "cycle_off" to 30, "min_run" to 10), s.settings.filterKeys { !it.startsWith("r") })
        assertEquals(24, s.settings.keys.count { it.startsWith("r") })
        assertEquals(65, s.settings["r1_value"])
        assertEquals(2, s.settings["r3_relay"])
        assertEquals(0, s.settings["r4_sensor"])
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
    fun relay2AndPhaseValues() {
        val s = DehumStateParser.parse(real.replace("\"relay2\":0", "\"relay2\":1").replace("\"phase\":\"on\"", "\"phase\":\"off\""))!!
        assertTrue(s.relay2)
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
