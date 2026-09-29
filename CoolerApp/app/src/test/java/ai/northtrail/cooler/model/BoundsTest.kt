package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class BoundsTest {
    @Test
    fun coversExactlyTheControllersSettings() {
        assertEquals(SETTING_KEYS, Bounds.ALL.map { it.key })
    }

    @Test
    fun everyStepperClampsToItsRange() {
        val expected = mapOf(
            "coolerset" to (2 to 40), "range" to (0 to 5), "sampleinterval" to (10 to 3600),
            "fin_cutoff" to (-5 to 5), "settle" to (2 to 30), "minofftime" to (0 to 30),
            "minruntime" to (0 to 600), "maxrun" to (1 to 60), "dutypercent" to (5 to 100),
        )
        for ((key, range) in expected) {
            assertEquals(key, range.first, Bounds.clamp(key, -10_000, finCutoff = -5))
            assertEquals(key, range.second, Bounds.clamp(key, 10_000, finCutoff = -5))
        }
    }

    @Test
    fun iceClearMustStayAboveIceCutoff() {
        assertEquals(4, Bounds.floor("fin_recover", finCutoff = 3))
        assertEquals(4, Bounds.clamp("fin_recover", 2, finCutoff = 3))
        assertEquals(-4, Bounds.floor("fin_recover", finCutoff = -5))
        assertEquals(10, Bounds.clamp("fin_recover", 50, finCutoff = 5))
        assertEquals(2, Bounds.floor("coolerset", finCutoff = 3))
    }

    @Test
    fun stepsMatchThePanel() {
        assertEquals(30, Bounds.find("minruntime")!!.step)
        assertEquals(5, Bounds.find("dutypercent")!!.step)
        assertEquals(Widget.PRESET, Bounds.find("sampleinterval")!!.widget)
    }

    @Test
    fun unknownKeysPassThrough() {
        assertNull(Bounds.find("turbo"))
        assertEquals(99, Bounds.clamp("turbo", 99, finCutoff = 0))
    }

    @Test
    fun valuesAreFormattedWithTheirUnit() {
        assertEquals("4 °C", Bounds.format("coolerset", 4))
        assertEquals("180 s", Bounds.format("minruntime", 180))
        assertEquals("50 %", Bounds.format("dutypercent", 50))
        assertEquals("1 h", Bounds.format("sampleinterval", 3600))
        assertEquals("5 m", Bounds.format("sampleinterval", 300))
        assertEquals("120 s", Bounds.format("sampleinterval", 120))
        assertEquals("--", Bounds.format("coolerset", null))
    }
}
