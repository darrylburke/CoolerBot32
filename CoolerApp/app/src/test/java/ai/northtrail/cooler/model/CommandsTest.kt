package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class CommandsTest {
    private val commands = Commands(Topics())

    @Test
    fun aSettingIsOneKeyOnTheCmdTopic() {
        assertEquals(Publish("cooler/cmd", """{"coolerset":5}"""), commands.setting("coolerset", 5))
        assertEquals(Publish("cooler/cmd", """{"fin_cutoff":-2}"""), commands.setting("fin_cutoff", -2))
    }

    @Test(expected = IllegalArgumentException::class)
    fun unknownSettingsAreRefused() {
        commands.setting("clearhist", 1)
    }

    @Test
    fun calibrationAndReset() {
        assertEquals(Publish("cooler/cmd", """{"calibrate":1}"""), commands.calibrate(start = true))
        assertEquals(Publish("cooler/cmd", """{"calibrate":0}"""), commands.calibrate(start = false))
        assertEquals(Publish("cooler/cmd", """{"fincal_reset":1}"""), commands.resetFinCal())
    }

    @Test
    fun followsTheTopicBase() {
        assertEquals("barn/cmd", Commands(Topics("barn")).setting("range", 2).topic)
    }
}
