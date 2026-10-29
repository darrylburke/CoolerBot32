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
        assertEquals("AUTO", DehumStatusText.mode(DehumState(mode = DehumMode.AUTO)))
        assertEquals("OFF", DehumStatusText.mode(DehumState(mode = DehumMode.OFF)))
        assertEquals("--", DehumStatusText.mode(null))
        assertEquals("--", DehumStatusText.mode(DehumState(mode = null)))
    }

    @Test
    fun autoLine() {
        assertEquals("no Cooler signal · dry", DehumStatusText.auto(DehumState(mode = DehumMode.AUTO, want = null, humid = false)))
        assertEquals("AC resting · humid", DehumStatusText.auto(DehumState(mode = DehumMode.AUTO, want = 1, humid = true)))
        assertEquals("AC cooling · dry", DehumStatusText.auto(DehumState(mode = DehumMode.AUTO, want = 0)))
        assertEquals("", DehumStatusText.auto(DehumState(mode = DehumMode.ON, want = 1)))
        assertEquals("", DehumStatusText.auto(null))
    }
}
