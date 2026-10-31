package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class DehumStatusTextTest {
    @Test
    fun modeLines() {
        assertEquals("TIMER 1 h 23 m left", DehumStatusText.mode(DehumState(mode = DehumMode.TIMER, remainS = 4_980)))
        assertEquals("TIMER 2 h left", DehumStatusText.mode(DehumState(mode = DehumMode.TIMER, remainS = 7_200)))
        assertEquals("TIMER 1 m left", DehumStatusText.mode(DehumState(mode = DehumMode.TIMER, remainS = 1)))
        assertEquals("CYCLE run 12 m left", DehumStatusText.mode(DehumState(mode = DehumMode.CYCLE, phaseOn = true, remainS = 700)))
        assertEquals("CYCLE rest 4 m left", DehumStatusText.mode(DehumState(mode = DehumMode.CYCLE, phaseOn = false, remainS = 240)))
        assertEquals("AUTO (rules)", DehumStatusText.mode(DehumState(mode = DehumMode.AUTO)))
        assertEquals("OFF", DehumStatusText.mode(DehumState(mode = DehumMode.OFF)))
        assertEquals("--", DehumStatusText.mode(null))
        assertEquals("--", DehumStatusText.mode(DehumState(mode = null)))
    }

    @Test
    fun autoLine() {
        assertEquals("no rule met", DehumStatusText.auto(DehumState(mode = DehumMode.AUTO)))
        assertEquals("rules met: 1, 3", DehumStatusText.auto(DehumState(mode = DehumMode.AUTO, ruleMatch = listOf(true, false, true, false))))
        assertEquals("", DehumStatusText.auto(DehumState(mode = DehumMode.ON, ruleMatch = listOf(true, false, false, false))))
        assertEquals("", DehumStatusText.auto(null))
    }

    @Test
    fun relaysLine() {
        assertEquals("Relay 1 on · Relay 2 off", DehumStatusText.relays(DehumState(relay = true)))
        assertEquals("Relay 1 off · Relay 2 on", DehumStatusText.relays(DehumState(relay2 = true)))
        assertEquals("", DehumStatusText.relays(null))
    }
}
