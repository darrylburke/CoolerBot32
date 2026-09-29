package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StatusTextTest {
    private fun st(mode: String, state: String) = CoolerState(mode = mode, state = state)

    @Test
    fun durPicksSecondsMinutesOrHours() {
        assertEquals("0s", StatusText.dur(0))
        assertEquals("59s", StatusText.dur(59))
        assertEquals("1m", StatusText.dur(60))
        assertEquals("59m", StatusText.dur(3599))
        assertEquals("1h 05m", StatusText.dur(3900))
    }

    @Test
    fun modeLabels() {
        assertEquals("NORMAL", StatusText.modeLabel(st("normal", "idle")))
        assertEquals("OVERRIDE", StatusText.modeLabel(st("override", "idle")))
        assertEquals("FIN PROXY", StatusText.modeLabel(st("fin-proxy", "idle")))
        assertEquals("OVR + FIN PROXY", StatusText.modeLabel(st("override-proxy", "idle")))
        assertEquals("BLIND TIMER", StatusText.modeLabel(st("blind", "idle")))
        assertEquals("turbo", StatusText.modeLabel(st("turbo", "idle")))
        assertEquals("--", StatusText.modeLabel(null))
        assertEquals("--", StatusText.modeLabel(CoolerState()))
    }

    @Test
    fun stateValueUsesTheTimerThatMattersForEachState() {
        assertEquals("Cooling 2m", StatusText.stateValue(st("normal", "cooling").copy(runS = 142)))
        assertEquals("Defrost 1m", StatusText.stateValue(st("normal", "defrost").copy(offS = 75)))
        assertEquals("Waiting 3m", StatusText.stateValue(st("normal", "wait").copy(holdS = 200)))
        assertEquals("Resting 6m", StatusText.stateValue(st("fin-proxy", "rest").copy(holdS = 360)))
        assertEquals("Resting", StatusText.stateValue(st("fin-proxy", "rest").copy(holdS = 0)))
        // Relay open, nothing needed: the AC's continuous fan is still running.
        assertEquals("Fan only 15m", StatusText.stateValue(st("normal", "idle").copy(offS = 900)))
        assertEquals("--", StatusText.stateValue(null))
    }

    @Test
    fun coolingInsideTheMinimumRunSaysSoWithTheTimeLeft() {
        val s = st("normal", "cooling").copy(runS = 142, holdS = 38)
        assertEquals("Cooling • min run 38s", StatusText.stateValue(s))
        assertEquals("Cooling 2m", StatusText.stateValue(s.copy(holdS = 0)))
    }

    @Test
    fun stateKeyIsTheModeWhenNotResting() {
        assertEquals("NORMAL", StatusText.stateKey(st("normal", "cooling").copy(holdS = 38)))
        assertEquals("NORMAL", StatusText.stateKey(st("normal", "cooling")))
        assertEquals("OVERRIDE", StatusText.stateKey(st("override", "wait").copy(holdS = 100)))
    }

    @Test
    fun compressorWording() {
        val s = st("normal", "cooling")
        assertEquals("Running", StatusText.compressor(s.copy(compressor = 1)))
        assertEquals("Starting", StatusText.compressor(s.copy(compressor = 0, relay = true)))
        assertEquals("Stopped", StatusText.compressor(s.copy(compressor = 0, relay = false)))
        assertEquals("--", StatusText.compressor(s.copy(compressor = null)))
        assertEquals("--", StatusText.compressor(null))
    }

    @Test
    fun finCalibrationSummary() {
        val s = st("normal", "idle")
        assertEquals("fin uncalibrated", StatusText.finCal(s))
        assertEquals("fin cal ok", StatusText.finCal(s.copy(finCal = true)))
        assertEquals(
            "calibrating 3 pts 6.2C",
            StatusText.finCal(s.copy(finCal = true, calActive = true, calPoints = 3, calSpan = 6.2)),
        )
        assertEquals("--", StatusText.finCal(null))
    }

    @Test
    fun stateKeyNamesWhatARestIsWaitingOn() {
        assertEquals("FIN PROXY - SETTLE", StatusText.stateKey(st("fin-proxy", "rest").copy(holdS = 360)))
        assertEquals("OVR + FIN PROXY - SETTLE", StatusText.stateKey(st("override-proxy", "rest").copy(holdS = 60)))
        assertEquals("BLIND TIMER - BACKUP DUTY", StatusText.stateKey(st("blind", "rest").copy(finFault = true, holdS = 180)))
        assertEquals("NORMAL - BACKUP DUTY", StatusText.stateKey(st("normal", "rest").copy(finFault = true, holdS = 180)))
        assertEquals("NORMAL", StatusText.stateKey(st("normal", "rest")))
    }

    @Test
    fun switchChipShowsTheOverrideSwitchPositionOrALostLink() {
        val s = st("normal", "idle")
        assertEquals(Chip("SWITCH OFF", false), StatusText.switchChip(s.copy(overrideSrc = "none")))
        assertEquals(Chip("SWITCH ON", true), StatusText.switchChip(s.copy(overrideSrc = "switch")))
        assertEquals(Chip("SWITCH ON", true), StatusText.switchChip(s.copy(overrideSrc = "both")))
        assertEquals(Chip("LINK LOST", true), StatusText.switchChip(s.copy(overrideSrc = "link")))
        assertEquals(Chip("--", false), StatusText.switchChip(CoolerState()))
        assertEquals(Chip("--", false), StatusText.switchChip(null))
    }

    @Test
    fun temperatureHasOneDecimal() {
        assertEquals("4.8", StatusText.temp(4.75))
        assertEquals("9.4", StatusText.temp(9.410621643))
        assertEquals("--", StatusText.temp(null))
    }

    @Test
    fun chipsListTheSwitchThenEveryActiveCondition() {
        assertEquals(listOf(Chip("--", false)), StatusText.chips(null))
        val quiet = st("normal", "idle").copy(overrideSrc = "none")
        assertEquals(listOf(Chip("SWITCH OFF", false)), StatusText.chips(quiet))
        val busy = quiet.copy(defrost = true, shtFault = true, finFault = true, noResponse = true, calActive = true)
        assertEquals(
            listOf(
                Chip("SWITCH OFF", false),
                Chip("DEFROST", true),
                Chip("BOX SENSOR FAULT", true),
                Chip("FIN SENSOR FAULT", true),
                Chip("AC NOT RESPONDING", true),
                Chip("CALIBRATING", false),
            ),
            StatusText.chips(busy),
        )
        assertTrue(StatusText.chips(busy).drop(1).dropLast(1).all { it.alert })
        assertFalse(StatusText.chips(busy).last().alert)
    }

    @Test
    fun setpointLineShowsTheOnAndOffPoints() {
        val s = CoolerState(settings = mapOf("coolerset" to 12, "range" to 2))
        assertEquals("set 12 ±2 · on >14 off <10", StatusText.setpointLine(s))
        assertEquals("--", StatusText.setpointLine(CoolerState(settings = mapOf("coolerset" to 12))))
        assertEquals("--", StatusText.setpointLine(null))
    }
}
