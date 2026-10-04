package ai.northtrail.cooler

import ai.northtrail.cooler.data.CoolerTransport
import ai.northtrail.cooler.data.IncomingMessage
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.LinkView
import ai.northtrail.cooler.model.Publish
import ai.northtrail.cooler.model.Sample
import ai.northtrail.cooler.model.Topics
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

private class FakeTransport : CoolerTransport {
    override val link = MutableStateFlow(LinkStatus())
    override val messages = MutableSharedFlow<IncomingMessage>(extraBufferCapacity = 64)
    val published = mutableListOf<Publish>()
    var publishSucceeds = true
    override fun connect() {}
    override fun disconnect() {}
    override fun publish(publish: Publish): Boolean {
        if (publishSucceeds) published += publish
        return publishSucceeds
    }
}

private fun payload(coolerset: Int = 4, finCutoff: Int = 1, finRecover: Int = 3, temp: Double = 4.8): String =
    """{"v":2,"temp":$temp,"humidity":70,"fin_temp":2.0,"mode":"normal","override_src":"none",""" +
        """"state":"idle","relay":0,"coolerset":$coolerset,"range":2,"sampleinterval":3600,""" +
        """"fin_cutoff":$finCutoff,"fin_recover":$finRecover,"settle":10,"minofftime":5,""" +
        """"minruntime":180,"maxrun":10,"dutypercent":50}"""

@OptIn(ExperimentalCoroutinesApi::class)
class CoolerControllerTest {
    private val transport = FakeTransport()
    private val topics = Topics()
    private val ignored = mutableListOf<String>()

    private fun TestScope.controller(): CoolerController =
        CoolerController(
            transport = transport,
            topics = topics,
            scope = backgroundScope,
            clock = { testScheduler.currentTime },
            configured = true,
            onIgnored = { ignored += it },
        ).also { it.start(); runCurrent() }

    private fun TestScope.emit(topic: String, body: String, retained: Boolean = false) {
        transport.messages.tryEmit(IncomingMessage(topic, body, retained))
        runCurrent()
    }

    /** Broker connected, controller online, retained /data delivered. */
    private fun TestScope.online(body: String = payload()) {
        transport.link.value = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = testScheduler.currentTime)
        runCurrent()
        emit("cooler/availability", "online", retained = true)
        emit("cooler/data", body, retained = true)
    }

    private fun cmd(json: String) = Publish("cooler/cmd", json)

    @Test
    fun retainedDataFillsTheStateButLeavesTheAgeUnknown() = runTest {
        val c = controller()
        online()
        val ui = c.ui.value
        assertEquals(4, ui.state!!.settings["coolerset"])
        assertEquals(LinkView.ONLINE, ui.health.link)
        assertNull(ui.health.dataAgeMillis)
        assertTrue(ui.trend.all.isEmpty())
    }

    @Test
    fun liveDataRecordsItsAgeAndATrendSample() = runTest {
        val c = controller()
        online()
        advanceTimeBy(5_001)
        runCurrent()
        emit("cooler/data", payload())
        assertEquals(0L, c.ui.value.health.dataAgeMillis)
        assertEquals(listOf(Sample(5, 4.8, false, 70.0)), c.ui.value.trend.all)
    }

    @Test
    fun aBadPayloadKeepsTheLastGoodState() = runTest {
        val c = controller()
        online()
        emit("cooler/data", """{"v":1}""")
        assertEquals(4, c.ui.value.state!!.settings["coolerset"])
        assertEquals(1, ignored.size)
    }

    @Test
    fun offlineDisablesControlsAndStepsDoNothing() = runTest {
        val c = controller()
        online()
        emit("cooler/availability", "offline", retained = true)
        assertEquals(LinkView.CONTROLLER_OFFLINE, c.ui.value.health.link)
        c.stepSetting("coolerset", +1)
        advanceTimeBy(1_000)
        runCurrent()
        assertTrue(transport.published.isEmpty())
        assertTrue(c.ui.value.drafts.isEmpty())
    }

    @Test
    fun aBurstOfTapsSendsOnePublishAfterTheDebounce() = runTest {
        val c = controller()
        online()
        repeat(3) { c.stepSetting("coolerset", +1) }
        assertEquals(7, c.ui.value.settingValue("coolerset"))
        assertTrue(c.ui.value.isPending("coolerset"))
        advanceTimeBy(399)
        runCurrent()
        assertTrue(transport.published.isEmpty())
        advanceTimeBy(2)
        runCurrent()
        assertEquals(listOf(cmd("""{"coolerset":7}""")), transport.published)
        assertEquals(7, c.ui.value.pending["coolerset"]!!.value)
        assertEquals(7, c.ui.value.settingValue("coolerset"))
    }

    @Test
    fun liveMatchingDataConfirms() = runTest {
        val c = controller()
        online()
        repeat(3) { c.stepSetting("coolerset", +1) }
        advanceTimeBy(500)
        emit("cooler/data", payload(coolerset = 7))
        assertFalse(c.ui.value.isPending("coolerset"))
        assertEquals(7, c.ui.value.settingValue("coolerset"))
        assertNull(c.ui.value.message)
    }

    @Test
    fun retainedMatchingDataDoesNotConfirm() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        emit("cooler/data", payload(coolerset = 5), retained = true)
        assertTrue(c.ui.value.isPending("coolerset"))
    }

    @Test
    fun staleLiveDataKeepsWaiting() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        emit("cooler/data", payload(coolerset = 4)) // periodic publish, sent before the cmd landed
        assertTrue(c.ui.value.isPending("coolerset"))
        assertNull(c.ui.value.message)
        advanceTimeBy(1_000)
        emit("cooler/data", payload(coolerset = 5))
        assertFalse(c.ui.value.isPending("coolerset"))
        assertNull(c.ui.value.message)
    }

    @Test
    fun noConfirmationRevertsAfterThreeSeconds() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        advanceTimeBy(4_000)
        runCurrent()
        assertFalse(c.ui.value.isPending("coolerset"))
        assertEquals(4, c.ui.value.settingValue("coolerset"))
        assertEquals("Not applied: Set point", c.ui.value.message)
    }

    @Test
    fun burstBackToCurrentSendsNothing() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        c.stepSetting("coolerset", -1)
        advanceTimeBy(500)
        runCurrent()
        assertTrue(transport.published.isEmpty())
        assertFalse(c.ui.value.isPending("coolerset"))
        advanceTimeBy(4_000)
        runCurrent()
        assertNull(c.ui.value.message)
    }

    @Test
    fun iceClearCannotGoBelowIceCutoffPlusOne() = runTest {
        val c = controller()
        online(payload(finCutoff = 3, finRecover = 4))
        c.stepSetting("fin_recover", -1)
        advanceTimeBy(500)
        runCurrent()
        assertTrue(transport.published.isEmpty())
    }

    @Test
    fun aPresetIsSentAfterTheDebounce() = runTest {
        val c = controller()
        online()
        c.chooseSetting("sampleinterval", 300)
        advanceTimeBy(500)
        runCurrent()
        assertEquals(listOf(cmd("""{"sampleinterval":300}""")), transport.published)
    }

    @Test
    fun aFailedPublishIsReportedAndNothingIsLeftPending() = runTest {
        val c = controller()
        online()
        transport.publishSucceeds = false
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        runCurrent()
        assertEquals("Couldn't send: controller not reachable", c.ui.value.message)
        assertFalse(c.ui.value.isPending("coolerset"))
        c.consumeMessage()
        assertNull(c.ui.value.message)
    }

    @Test
    fun calibrationActionsPublishImmediately() = runTest {
        val c = controller()
        online()
        c.startCalibration()
        c.abortCalibration()
        c.resetFinCal()
        assertEquals(
            listOf(cmd("""{"calibrate":1}"""), cmd("""{"calibrate":0}"""), cmd("""{"fincal_reset":1}""")),
            transport.published,
        )
    }

    @Test
    fun silentAfterFiveMinutesDisablesControls() = runTest {
        val c = controller()
        online()
        emit("cooler/data", payload())
        advanceTimeBy(301_000)
        runCurrent()
        assertEquals(LinkView.SILENT, c.ui.value.health.link)
        assertFalse(c.ui.value.health.controlsEnabled)
    }

    @Test
    fun resetForgetsOldCoolerAndFollowsNewTopics() = runTest {
        val c = controller()
        online()
        c.reset(Topics("barn"))
        assertNull(c.ui.value.state)
        emit("cooler/data", payload(coolerset = 8))
        assertNull(c.ui.value.state)
        emit("barn/data", payload(coolerset = 9))
        assertEquals(9, c.ui.value.state!!.settings["coolerset"])
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        runCurrent()
        assertEquals(listOf(Publish("barn/cmd", """{"coolerset":10}""")), transport.published)
    }

    @Test
    fun aPendingEditSurvivesALinkBlipWhenLiveDataFollows() = runTest {
        val c = controller()
        online()
        c.stepSetting("coolerset", +1)
        advanceTimeBy(500)
        transport.link.value = LinkStatus(LinkState.DISCONNECTED, "gone")
        runCurrent()
        advanceTimeBy(500)
        transport.link.value = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = testScheduler.currentTime)
        runCurrent()
        assertTrue(c.ui.value.isPending("coolerset"))
        emit("cooler/data", payload(coolerset = 5))
        assertFalse(c.ui.value.isPending("coolerset"))
        assertNull(c.ui.value.message)
    }

    @Test
    fun reconnectingForgetsTheAgeOfTheLastSession() = runTest {
        val c = controller()
        online()
        emit("cooler/data", payload())
        advanceTimeBy(5_001)
        runCurrent()
        assertEquals(5_000L, c.ui.value.health.dataAgeMillis)
        transport.link.value = LinkStatus(LinkState.DISCONNECTED, "off")
        runCurrent()
        advanceTimeBy(60_000)
        transport.link.value = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = testScheduler.currentTime)
        runCurrent()
        emit("cooler/data", payload(), retained = true)
        assertNull(c.ui.value.health.dataAgeMillis)
        emit("cooler/data", payload())
        assertEquals(0L, c.ui.value.health.dataAgeMillis)
    }

    @Test
    fun historyFillsTheTrend() = runTest {
        val c = controller()
        online()
        emit(
            "cooler/history",
            """{"v":1,"t0":1790700000,"interval_s":60,"temp":[4.2,null,4.4],"hum":[80,81,82],"relay":[0,1,1]}""",
        )
        assertEquals(listOf(Sample(1_790_700_000, 4.2, false, 80.0), Sample(1_790_700_120, 4.4, true, 82.0)), c.ui.value.trend.all)
    }

    @Test
    fun aBadHistoryIsIgnoredAndReported() = runTest {
        val c = controller()
        online()
        emit("cooler/history", """{"v":7}""")
        assertTrue(c.ui.value.trend.all.isEmpty())
        assertEquals(listOf("ignored cooler/history: not cooler/history v1"), ignored)
    }
}
