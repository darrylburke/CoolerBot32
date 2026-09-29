package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CoolerStateParserTest {
    // A real payload captured from the controller on 2026-09-28 (history shortened).
    private val full = """
        {"v":2,"temp":9.410621643,"humidity":73.50270844,"fin_temp":21.97052002,"fin_ohms":11456.79102,
         "fin_slope":-0.226509839,"mode":"normal","override_src":"none","state":"idle","relay":0,"cool_call":0,
         "defrost":0,"compressor":0,"sht_fault":0,"fin_fault":0,"no_response":0,"run_s":0,"off_s":39,"hold_s":0,
         "coolerset":12,"range":2,"maxrun":10,"dutypercent":50,"minofftime":5,"minruntime":180,
         "sampleinterval":3600,"fin_cutoff":1,"fin_recover":3,"settle":10,"fin_cal":0,"fin_beta":3950,
         "fin_r0":10000,"cal_active":0,"cal_points":0,"cal_span":0,"hist_n":3,"hist_interval_s":3600,
         "hist_last_ts":1790640000,"temp_hist":[21,20,19],"hum_hist":[60,61,62],"uptime_s":5025}
    """.trimIndent()

    @Test
    fun parsesEveryFieldTheAppUses() {
        val s = CoolerStateParser.parse(full)!!
        assertEquals(9.410621643, s.temp!!, 1e-9)
        assertEquals(73.50270844, s.humidity!!, 1e-9)
        assertEquals(21.97052002, s.finTemp!!, 1e-9)
        assertEquals(11456.79102, s.finOhms!!, 1e-6)
        assertEquals(-0.226509839, s.finSlope!!, 1e-9)
        assertEquals("normal", s.mode)
        assertEquals("none", s.overrideSrc)
        assertEquals("idle", s.state)
        assertFalse(s.relay)
        assertFalse(s.coolCall)
        assertFalse(s.defrost)
        assertEquals(0, s.compressor)
        assertFalse(s.shtFault || s.finFault || s.noResponse)
        assertEquals(0L, s.runS)
        assertEquals(39L, s.offS)
        assertEquals(0L, s.holdS)
        assertEquals(SETTING_KEYS.toSet(), s.settings.keys)
        assertEquals(12, s.settings["coolerset"])
        assertEquals(3600, s.settings["sampleinterval"])
        assertEquals(1, s.settings["fin_cutoff"])
        assertFalse(s.finCal)
        assertFalse(s.calActive)
        assertEquals(3600, s.histIntervalS)
        assertEquals(1_790_640_000L, s.histLastTs)
        assertEquals(listOf(21.0, 20.0, 19.0), s.tempHist)
        assertEquals(5025L, s.uptimeS)
    }

    @Test
    fun nullsBecomeUnknown() {
        val s = CoolerStateParser.parse(
            """{"v":2,"temp":null,"humidity":null,"fin_temp":null,"compressor":null,"state":"cooling","relay":1}""",
        )!!
        assertNull(s.temp)
        assertNull(s.humidity)
        assertNull(s.finTemp)
        assertNull(s.compressor)
        assertEquals("cooling", s.state)
        assertTrue(s.relay)
        assertTrue(s.settings.isEmpty())
        assertTrue(s.tempHist.isEmpty())
    }

    @Test
    fun aBareVersionIsAnEmptyState() {
        assertEquals(CoolerState(), CoolerStateParser.parse("""{"v":2}"""))
    }

    @Test
    fun otherVersionsAreIgnored() {
        assertNull(CoolerStateParser.parse("""{"v":1,"temp":4.0}"""))
        assertNull(CoolerStateParser.parse("""{"temp":4.0}"""))
    }

    @Test
    fun anythingThatIsNotAJsonObjectIsIgnored() {
        assertNull(CoolerStateParser.parse("hello"))
        assertNull(CoolerStateParser.parse("[1,2]"))
        assertNull(CoolerStateParser.parse(""))
    }

    @Test
    fun wrongTypesAreTreatedAsMissing() {
        val s = CoolerStateParser.parse(
            """{"v":2,"temp":"warm","relay":"yes","coolerset":"x","mode":5,"temp_hist":[1,"a",null,2]}""",
        )!!
        assertNull(s.temp)
        assertFalse(s.relay)
        assertNull(s.settings["coolerset"])
        assertNull(s.mode)
        assertEquals(listOf(1.0, 2.0), s.tempHist)
    }
}
