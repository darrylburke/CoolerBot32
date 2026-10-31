package ai.northtrail.cooler

import ai.northtrail.cooler.data.CoolerTransport
import ai.northtrail.cooler.data.IncomingMessage
import ai.northtrail.cooler.model.DehumMode
import ai.northtrail.cooler.model.DehumView
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.Publish
import ai.northtrail.cooler.model.Sample
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

private class FakeDehumTransport : CoolerTransport {
    override val link = MutableStateFlow(LinkStatus())
    override val messages = MutableSharedFlow<IncomingMessage>(extraBufferCapacity = 64)
    val published = mutableListOf<Publish>()
    override fun connect() {}
    override fun disconnect() {}
    override fun publish(publish: Publish): Boolean { published += publish; return true }
}

private fun data(
    mode: String = "cycle", humidity: String = "54.5", temp: String = "23.6", cycleOn: Int = 30, minRun: Int = 10,
    r1Sensor: Int = 1, r1Value: Int = 65,
) =
    """{"v":1,"mode":"$mode","relay":1,"relay2":0,"link_lost":0,"hold_s":0,"hold2_s":0,"remain_s":300,"phase":"on",""" +
        """"temp":$temp,"humidity":$humidity,"min_run":$minRun,"timer_min":120,""" +
        """"r1_en":1,"r1_sensor":$r1Sensor,"r1_cmp":1,"r1_value":$r1Value,"r1_relay":1,"r1_action":1,"r1_match":0,""" +
        """"r2_en":0,"r2_sensor":1,"r2_cmp":0,"r2_value":55,"r2_relay":1,"r2_action":0,"r2_match":0,""" +
        """"r3_en":0,"r3_sensor":0,"r3_cmp":1,"r3_value":10,"r3_relay":2,"r3_action":1,"r3_match":0,""" +
        """"r4_en":0,"r4_sensor":0,"r4_cmp":0,"r4_value":2,"r4_relay":2,"r4_action":1,"r4_match":0,""" +
        """"cycle_on":$cycleOn,"cycle_off":30,"run_s":25,"uptime_s":20}"""

@OptIn(ExperimentalCoroutinesApi::class)
class DehumControllerTest {
    private val transport = FakeDehumTransport()

    private fun TestScope.controller(): DehumController =
        DehumController(transport, backgroundScope, clock = { testScheduler.currentTime })
            .also { it.start(); runCurrent() }

    private fun TestScope.emit(topic: String, body: String, retained: Boolean = false) {
        transport.messages.tryEmit(IncomingMessage(topic, body, retained))
        runCurrent()
    }

    private fun TestScope.connected() {
        transport.link.value = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = testScheduler.currentTime)
        runCurrent()
    }

    private fun cmd(json: String) = Publish("dehum/cmd", json)

    @Test
    fun liveDataGoesOnlineAndIntoTheTrend() = runTest {
        val c = controller()
        connected()
        advanceTimeBy(5_000); runCurrent()
        emit("dehum/data", data())
        val ui = c.ui.value
        assertEquals(DehumView.ONLINE, ui.view)
        assertEquals(DehumMode.CYCLE, ui.state!!.mode)
        assertEquals(listOf(Sample(5, 23.6, true, 54.5)), ui.trend.all)
    }

    @Test
    fun temperatureNullIsAGapNotASample() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data(temp = "null"))
        assertNull(c.ui.value.state!!.temp)
        assertTrue(c.ui.value.trend.all.isEmpty())
    }

    @Test
    fun humidityNullKeepsTheTemperature() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data(humidity = "null"))
        assertEquals(listOf(Sample(0, 23.6, true, null)), c.ui.value.trend.all)
    }

    @Test
    fun modeTapPublishesAtOnce() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data())
        c.setMode(DehumMode.TIMER)
        assertEquals(listOf(cmd("""{"mode":"timer"}""")), transport.published)
    }

    @Test
    fun stepUsesTheDehumStepAndFloor() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data())
        c.stepSetting("cycle_on", +1)
        advanceTimeBy(DehumController.DEBOUNCE_MS + 1); runCurrent()
        assertEquals(listOf(cmd("""{"cycle_on":35}""")), transport.published)
        assertTrue(c.ui.value.isPending("cycle_on"))
        advanceTimeBy(100); runCurrent()
        emit("dehum/data", data(cycleOn = 35))
        assertFalse(c.ui.value.isPending("cycle_on"))
        assertEquals(35, c.ui.value.settingValue("cycle_on"))

        transport.published.clear()
        emit("dehum/data", data(cycleOn = 10))
        c.stepSetting("cycle_on", -1)
        advanceTimeBy(1_000); runCurrent()
        assertTrue(transport.published.isEmpty())
    }

    @Test
    fun pendingTimesOut() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data())
        c.stepSetting("cycle_on", +1)
        advanceTimeBy(DehumController.DEBOUNCE_MS + 4_000); runCurrent()
        assertFalse(c.ui.value.isPending("cycle_on"))
        assertEquals("Not applied: Cycle run", c.ui.value.message)
    }

    @Test
    fun enterSettingChecksTheLiveRange() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data())
        assertFalse(c.enterSetting("cycle_on", 5))
        assertFalse(c.enterSetting("coolerset", 5))
        assertFalse(c.enterSetting("dehum_rh", 70))
        assertTrue(c.enterSetting("r1_value", 70))
        advanceTimeBy(DehumController.DEBOUNCE_MS + 1); runCurrent()
        assertEquals(listOf(cmd("""{"r1_value":70}""")), transport.published)
    }

    @Test
    fun ruleThresholdRangeFollowsItsSensor() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data(r1Sensor = 1, r1Value = 98))
        assertFalse(c.enterSetting("r1_value", -5))     // humidity: 0..100
        c.stepSetting("r1_value", +1); c.stepSetting("r1_value", +1); c.stepSetting("r1_value", +1)
        assertEquals(100, c.ui.value.settingValue("r1_value"))
        emit("dehum/data", data(r1Sensor = 0, r1Value = 20))
        advanceTimeBy(DehumController.DEBOUNCE_MS + 10_000); runCurrent()
        assertTrue(c.enterSetting("r1_value", -5))      // temperature: -20..50
        assertFalse(c.enterSetting("r1_value", 51))
    }

    @Test
    fun ruleChoicesAndEnableAreSentAndPendUntilEchoed() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data())
        assertTrue(c.enterSetting("r2_en", 1))
        assertTrue(c.enterSetting("r3_relay", 1))
        assertFalse(c.enterSetting("r3_relay", 3))
        assertFalse(c.enterSetting("r2_action", 2))
        advanceTimeBy(DehumController.DEBOUNCE_MS + 1); runCurrent()
        assertEquals(setOf(cmd("""{"r2_en":1}"""), cmd("""{"r3_relay":1}""")), transport.published.toSet())
        assertTrue(c.ui.value.isPending("r2_en"))
        emit("dehum/data", data().replace("\"r2_en\":0", "\"r2_en\":1").replace("\"r3_relay\":2", "\"r3_relay\":1"))
        assertFalse(c.ui.value.isPending("r2_en"))
        assertFalse(c.ui.value.isPending("r3_relay"))
    }

    @Test
    fun aTimedOutRuleEditIsNamed() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data())
        assertTrue(c.enterSetting("r2_value", 50))
        advanceTimeBy(DehumController.DEBOUNCE_MS + 1); runCurrent()
        // The live echoes keep saying 55, so the edit times out.
        repeat(6) { advanceTimeBy(1_000); emit("dehum/data", data()) }
        assertEquals("Not applied: Rule 2 threshold", c.ui.value.message)
    }

    @Test
    fun dehumHealthIsIndependentOfTheCooler() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data())
        emit("cooler/availability", "offline", retained = true)
        assertEquals(DehumView.ONLINE, c.ui.value.view)
        assertTrue(c.ui.value.controlsEnabled)
    }

    @Test
    fun retainedDataAloneGoesSilentAfter90s() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data(), retained = true)
        assertEquals(DehumView.ONLINE, c.ui.value.view)
        assertTrue(c.ui.value.trend.all.isEmpty())
        advanceTimeBy(91_000); runCurrent()
        assertEquals(DehumView.SILENT, c.ui.value.view)
        assertFalse(c.ui.value.controlsEnabled)
        c.setMode(DehumMode.ON)
        assertTrue(transport.published.isEmpty())
        assertEquals("Couldn't send: dehumidifier not reachable", c.ui.value.message)
    }

    @Test
    fun offlineAvailabilityDisablesControls() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data())
        emit("dehum/availability", "offline", retained = true)
        assertEquals(DehumView.OFFLINE, c.ui.value.view)
        assertFalse(c.ui.value.controlsEnabled)
    }

    @Test
    fun historyMerges() = runTest {
        val t0 = 1_790_700_000L
        // A real-date clock without running the 1 s ticker for 56 years of virtual time.
        val c = DehumController(transport, backgroundScope, clock = { (t0 + 180) * 1_000 + testScheduler.currentTime })
            .also { it.start(); runCurrent() }
        connected()
        emit("dehum/history", """{"v":1,"t0":$t0,"interval_s":60,"hum":[55,56],"temp":[23,23],"relay":[1,0]}""")
        assertEquals(listOf(Sample(t0, 23.0, true, 55.0), Sample(t0 + 60, 23.0, false, 56.0)), c.ui.value.trend.all)
    }
}
