package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DehumBoundsTest {
    private val none: (String) -> Int? = { null }

    @Test
    fun keysAndOrder() {
        assertEquals(listOf("timer_min", "cycle_on", "cycle_off", "min_run"), DehumBounds.ALL.map { it.key })
        assertEquals(4 + 24, DehumBounds.KEYS.size)
        assertEquals(listOf("r1_en", "r1_sensor", "r1_cmp", "r1_value", "r1_relay", "r1_action"), DehumBounds.KEYS.subList(4, 10))
        assertEquals("r4_action", DehumBounds.KEYS.last())
    }

    @Test
    fun settingRanges() {
        assertEquals(10..240, DehumBounds.range("cycle_on") { if (it == "min_run") 10 else null })
        assertEquals(1..240, DehumBounds.range("cycle_on", none))
        assertEquals(0..60, DehumBounds.range("min_run", none))
        assertEquals(1..1440, DehumBounds.range("timer_min", none))
        assertNull(DehumBounds.range("dehum_rh", none))
        assertNull(DehumBounds.range("r5_value", none))
    }

    @Test
    fun ruleRangesFollowTheSensor() {
        assertEquals(-20..50, DehumBounds.range("r2_value") { if (it == "r2_sensor") 0 else null })
        assertEquals(0..100, DehumBounds.range("r2_value") { if (it == "r2_sensor") 1 else null })
        assertEquals(1..2, DehumBounds.range("r3_relay", none))
        for (f in listOf("en", "sensor", "cmp", "action")) assertEquals(0..1, DehumBounds.range("r1_$f", none))
    }

    @Test
    fun steps() {
        assertEquals(mapOf("timer_min" to 15, "cycle_on" to 5, "cycle_off" to 5, "min_run" to 1), DehumBounds.ALL.associate { it.key to it.step })
        assertEquals(1, DehumBounds.step("r1_value"))
    }

    @Test
    fun formatAndLabels() {
        assertEquals("120 min", DehumBounds.format("timer_min", 120))
        assertEquals("--", DehumBounds.format("timer_min", null))
        assertEquals("65 %RH", DehumBounds.format("r1_value", 65) { if (it == "r1_sensor") 1 else null })
        assertEquals("-3 °C", DehumBounds.format("r1_value", -3) { if (it == "r1_sensor") 0 else null })
        assertEquals("Cycle run", DehumBounds.label("cycle_on"))
        assertEquals("Rule 2 threshold", DehumBounds.label("r2_value"))
        assertEquals("Rule 4 enabled", DehumBounds.label("r4_en"))
        assertEquals(3 to "relay", DehumBounds.parseRuleKey("r3_relay"))
        assertNull(DehumBounds.parseRuleKey("r0_relay"))
    }
}
