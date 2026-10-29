package ai.northtrail.cooler.ui

import ai.northtrail.cooler.DehumUi
import ai.northtrail.cooler.model.DehumMode
import ai.northtrail.cooler.model.DehumState
import ai.northtrail.cooler.model.DehumView
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class HumidityScreenTest {
    @get:Rule val rule = createComposeRule()
    private val modes = mutableListOf<DehumMode>()

    private val settings = mapOf("timer_h" to 2, "cycle_on" to 30, "cycle_off" to 30, "min_run" to 10, "dehum_rh" to 65)
    private val base = DehumUi(
        view = DehumView.ONLINE,
        state = DehumState(mode = DehumMode.CYCLE, phaseOn = true, remainS = 700, humidity = 54.4, settings = settings),
    )

    private fun show(ui: DehumUi) = rule.setContent {
        CoolerTheme {
            HumidityScreen(ui, nowEpochS = 1_790_700_000L, onMode = { modes += it }, onStep = { _, _ -> }, onEnter = { _, _ -> })
        }
    }

    @Test
    fun graphShowsTheReadingAndMode() {
        show(base)
        rule.onNodeWithText("54", substring = true).assertExists()
        rule.onNodeWithText("CYCLE run 12 m left", substring = true).assertExists()
    }

    @Test
    fun controlsShowFiveModesWithTheCurrentOneSelected() {
        show(base)
        rule.onNodeWithText("Controls").performClick()
        listOf("on", "off", "timer", "auto", "cycle").forEach { rule.onNodeWithTag("mode-$it").assertExists() }
        rule.onNodeWithTag("mode-cycle").assertIsSelected()
        rule.onNodeWithTag("mode-timer").performClick()
        assertEquals(listOf(DehumMode.TIMER), modes)
        rule.onNodeWithTag("value-dehum_rh").assertExists()
    }

    @Test
    fun offlineDisablesTheControls() {
        show(base.copy(view = DehumView.OFFLINE))
        rule.onNodeWithText("Controls").performClick()
        rule.onNodeWithTag("mode-on").assertIsNotEnabled()
        rule.onNodeWithText("Controls work only while the dehumidifier is online.").assertExists()
    }
}
