package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class DehumCommandsTest {
    private val c = DehumCommands()

    @Test
    fun modeAndSettingPayloads() {
        assertEquals(Publish("dehum/cmd", """{"mode":"timer"}"""), c.mode(DehumMode.TIMER))
        assertEquals(Publish("dehum/cmd", """{"cycle_off":45}"""), c.setting("cycle_off", 45))
    }

    @Test(expected = IllegalArgumentException::class)
    fun refusesCoolerKeys() {
        c.setting("coolerset", 4)
    }
}
