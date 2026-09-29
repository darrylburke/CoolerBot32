package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class DetailRowsTest {
    private val health = Health(LinkView.ONLINE, "Online", emptyList(), 0L)

    @Test
    fun withoutDataOnlyTheLinkIsShown() {
        assertEquals(listOf("Link" to "Online"), DetailRows.of(null, health))
    }

    @Test
    fun everyFieldIsRendered() {
        val s = CoolerState(
            mode = "override", overrideSrc = "switch", state = "cooling", relay = true, coolCall = true,
            runS = 142, offS = 0, holdS = 38, compressor = 1, finTemp = 1.25, finOhms = 11456.79,
            finSlope = -0.2265, shtFault = false, finFault = true, noResponse = true, uptimeS = 5025,
        )
        assertEquals(
            listOf(
                "Link" to "Online",
                "Mode" to "OVERRIDE",
                "Override source" to "switch",
                "State" to "Cooling • min run 38s",
                "Relay" to "Closed (cooling)",
                "Cooling call" to "Yes",
                "Run" to "2m",
                "Off" to "0s",
                "Hold" to "38s",
                "Compressor" to "Running",
                "Coil" to "1.3 °C",
                "Fin resistance" to "11457 Ω",
                "Fin slope" to "-0.23 °C/min",
                "Box sensor" to "OK",
                "Fin sensor" to "Fault",
                "AC response" to "Not responding",
                "Uptime" to "1h 23m",
            ),
            DetailRows.of(s, health),
        )
    }

    @Test
    fun missingNumbersAreDashes() {
        val rows = DetailRows.of(CoolerState(), health).toMap()
        assertEquals("--", rows["Fin resistance"])
        assertEquals("--", rows["Fin slope"])
        assertEquals("--", rows["Uptime"])
        assertEquals("-- °C", rows["Coil"])
        assertEquals("--", rows["Override source"])
    }
}
