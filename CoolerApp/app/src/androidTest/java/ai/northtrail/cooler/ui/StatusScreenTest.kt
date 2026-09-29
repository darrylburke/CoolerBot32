package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.Banner
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import org.junit.Rule
import org.junit.Test

class StatusScreenTest {
    @get:Rule val rule = createComposeRule()

    private val online = Health(LinkView.ONLINE, "Online", emptyList(), 12_000L)

    private fun show(ui: UiState) = rule.setContent {
        CoolerTheme { StatusScreen(ui, nowEpochS = 1_790_640_000L) }
    }

    @Test
    fun showsTemperatureStateAndSwitch() {
        val s = CoolerState(
            temp = 4.75, humidity = 78.2, finTemp = 1.2, mode = "normal", overrideSrc = "switch",
            state = "cooling", relay = true, holdS = 38, compressor = 0,
            settings = mapOf("coolerset" to 4, "range" to 2),
        )
        show(UiState(configured = true, state = s, health = online))
        rule.onNodeWithText("4.8 °C").assertExists()
        rule.onNodeWithText("78 %RH").assertExists()
        rule.onNodeWithText("set 4 ±2 · on >6 off <2").assertExists()
        rule.onNodeWithText("NORMAL").assertExists()
        rule.onNodeWithText("Cooling • min run 38s").assertExists()
        rule.onNodeWithText("SWITCH ON").assertExists()
        rule.onNodeWithText("Coil 1.2 °C").assertExists()
        rule.onNodeWithText("Compressor Starting").assertExists()
        rule.onNodeWithText("Online · updated 12s ago", substring = true).assertExists()
    }

    @Test
    fun showsBannersAndDashesWithoutData() {
        show(
            UiState(
                configured = true,
                health = Health(LinkView.CONTROLLER_OFFLINE, "Controller offline", listOf(Banner.CONTROLLER_OFFLINE), null),
            ),
        )
        rule.onAllNodesWithText("Controller offline", substring = true).assertCountEquals(2) // strip + banner
        rule.onNodeWithText("-- °C").assertExists()
        rule.onNodeWithText("No history yet").assertExists()
    }
}
