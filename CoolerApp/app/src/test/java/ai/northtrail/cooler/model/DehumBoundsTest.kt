package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class DehumBoundsTest {
    @Test
    fun keysAndOrder() {
        assertEquals(listOf("timer_min", "cycle_on", "cycle_off", "min_run", "dehum_rh"), DehumBounds.KEYS)
        assertEquals(DehumBounds.KEYS, DehumBounds.ALL.map { it.key })
    }

    @Test
    fun floorsAndCeilings() {
        assertEquals(10, DehumBounds.floor("cycle_on", 10))
        assertEquals(1, DehumBounds.floor("cycle_on", 0))
        assertEquals(0, DehumBounds.floor("min_run", 10))
        assertEquals(1, DehumBounds.floor("timer_min", 10))
        assertEquals(30, DehumBounds.floor("dehum_rh", 10))
        assertEquals(95, DehumBounds.hi("dehum_rh"))
        assertEquals(1440, DehumBounds.hi("timer_min"))
        assertEquals(240, DehumBounds.hi("cycle_off"))
        assertEquals(60, DehumBounds.hi("min_run"))
    }

    @Test
    fun steps() {
        val step = DehumBounds.ALL.associate { it.key to it.step }
        assertEquals(mapOf("timer_min" to 15, "cycle_on" to 5, "cycle_off" to 5, "min_run" to 1, "dehum_rh" to 1), step)
    }

    @Test
    fun format() {
        assertEquals("65 %RH", DehumBounds.format("dehum_rh", 65))
        assertEquals("120 min", DehumBounds.format("timer_min", 120))
        assertEquals("--", DehumBounds.format("timer_min", null))
    }
}
