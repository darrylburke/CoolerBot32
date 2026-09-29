package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HealthTest {
    private val connected = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = 0)
    private val state = CoolerState(mode = "normal", state = "idle")

    private fun eval(
        link: LinkStatus = connected,
        online: Boolean? = true,
        s: CoolerState? = state,
        lastLive: Long? = null,
        now: Long = 1_000,
    ) = Liveness.evaluate(link, online, s, lastLive, now)

    @Test
    fun brokerProblemsComeFirst() {
        assertEquals("Broker refused access", eval(link = LinkStatus(LinkState.REJECTED)).label)
        assertEquals(
            "Can't verify the broker (bad cert)",
            eval(link = LinkStatus(LinkState.TLS_FAILED, "bad cert")).label,
        )
        assertEquals("Connecting…", eval(link = LinkStatus(LinkState.CONNECTING)).label)
        assertEquals("Broker unreachable", eval(link = LinkStatus(LinkState.DISCONNECTED)).label)
        assertFalse(eval(link = LinkStatus(LinkState.DISCONNECTED)).controlsEnabled)
    }

    @Test
    fun connectedWithoutDataIsWaiting() {
        val h = eval(s = null)
        assertEquals(LinkView.WAITING, h.link)
        assertEquals("Waiting for controller", h.label)
        assertFalse(h.controlsEnabled)
    }

    @Test
    fun controllerOfflineDisablesControlsAndRaisesABanner() {
        val h = eval(online = false)
        assertEquals(LinkView.CONTROLLER_OFFLINE, h.link)
        assertEquals(listOf(Banner.CONTROLLER_OFFLINE), h.banners)
        assertFalse(h.controlsEnabled)
    }

    @Test
    fun retainedDataAloneIsOnlineWithUnknownAge() {
        val h = eval(lastLive = null, now = 5_000)
        assertEquals(LinkView.ONLINE, h.link)
        assertTrue(h.controlsEnabled)
        assertNull(h.dataAgeMillis)
        assertEquals("Online · waiting for update", h.stripText())
    }

    @Test
    fun silentAfterFiveMinutesWithoutLiveData() {
        assertEquals(LinkView.ONLINE, eval(lastLive = 0, now = 300_000).link)
        val h = eval(lastLive = 0, now = 300_001)
        assertEquals(LinkView.SILENT, h.link)
        assertEquals("No data for 5 min", h.label)
        assertEquals(listOf(Banner.CONTROLLER_SILENT), h.banners)
        assertFalse(h.controlsEnabled)
    }

    @Test
    fun reconnectingRestartsTheSilenceClock() {
        val h = eval(link = connected.copy(connectedAtMillis = 400_000), lastLive = 0, now = 500_000)
        assertEquals(LinkView.ONLINE, h.link)
    }

    @Test
    fun faultBannersFollowTheStateEvenWhenTheBrokerIsDown() {
        val faulty = state.copy(noResponse = true, shtFault = true, finFault = true)
        assertEquals(
            listOf(Banner.AC_NOT_RESPONDING, Banner.BOX_SENSOR_FAULT, Banner.FIN_SENSOR_FAULT),
            eval(s = faulty).banners,
        )
        assertEquals(
            listOf(Banner.AC_NOT_RESPONDING, Banner.BOX_SENSOR_FAULT, Banner.FIN_SENSOR_FAULT),
            eval(link = LinkStatus(LinkState.DISCONNECTED), s = faulty).banners,
        )
    }

    @Test
    fun stripTextCarriesTheAge() {
        assertEquals("Online · updated 12s ago", eval(lastLive = 988_000, now = 1_000_000).stripText())
        assertEquals(
            "Broker unreachable · last data 14m ago",
            eval(link = LinkStatus(LinkState.DISCONNECTED), lastLive = 0, now = 840_000).stripText(),
        )
        assertEquals("Broker unreachable", eval(link = LinkStatus(LinkState.DISCONNECTED)).stripText())
    }

    @Test
    fun ageFormatting() {
        assertEquals("59s", formatAge(59_999))
        assertEquals("1m", formatAge(60_000))
        assertEquals("2h", formatAge(7_200_000))
    }
}
