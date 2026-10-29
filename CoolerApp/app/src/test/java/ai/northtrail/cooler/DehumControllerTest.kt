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

private fun data(mode: String = "cycle", humidity: String = "54.5", cycleOn: Int = 30, minRun: Int = 10, rh: Int = 65) =
    """{"v":1,"mode":"$mode","relay":1,"want":null,"link_lost":0,"hold_s":0,"remain_s":300,"phase":"on",""" +
        """"temp":23.6,"humidity":$humidity,"humid":0,"dehum_rh":$rh,"min_run":$minRun,"timer_min":120,""" +
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
        assertEquals(listOf(Sample(5, 54.5, true)), ui.trend.all)
    }

    @Test
    fun humidityNullIsAGapNotASample() = runTest {
        val c = controller()
        connected()
        emit("dehum/data", data(humidity = "null"))
        assertNull(c.ui.value.state!!.humidity)
        assertTrue(c.ui.value.trend.all.isEmpty())
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
        assertTrue(c.enterSetting("dehum_rh", 70))
        advanceTimeBy(DehumController.DEBOUNCE_MS + 1); runCurrent()
        assertEquals(listOf(cmd("""{"dehum_rh":70}""")), transport.published)
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
        assertEquals(listOf(Sample(t0, 55.0, true), Sample(t0 + 60, 56.0, false)), c.ui.value.trend.all)
    }
}
