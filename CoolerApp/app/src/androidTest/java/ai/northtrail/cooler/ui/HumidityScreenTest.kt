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
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class HumidityScreenTest {
    @get:Rule val rule = createComposeRule()
    private val modes = mutableListOf<DehumMode>()
    private val entered = mutableListOf<Pair<String, Int>>()

    private val rules = mapOf(
        "r1_en" to 1, "r1_sensor" to 1, "r1_cmp" to 1, "r1_value" to 65, "r1_relay" to 1, "r1_action" to 1,
        "r2_en" to 0, "r2_sensor" to 1, "r2_cmp" to 0, "r2_value" to 55, "r2_relay" to 1, "r2_action" to 0,
        "r3_en" to 1, "r3_sensor" to 0, "r3_cmp" to 1, "r3_value" to 10, "r3_relay" to 2, "r3_action" to 1,
        "r4_en" to 0, "r4_sensor" to 0, "r4_cmp" to 0, "r4_value" to 2, "r4_relay" to 2, "r4_action" to 1,
    )
    private val settings = mapOf("timer_min" to 120, "cycle_on" to 30, "cycle_off" to 30, "min_run" to 10) + rules
    private val base = DehumUi(
        view = DehumView.ONLINE,
        state = DehumState(
            mode = DehumMode.CYCLE, phaseOn = true, remainS = 700, temp = 3.4, humidity = 54.4, settings = settings,
            ruleMatch = listOf(false, false, true, false), relay2 = true,
        ),
    )

    private fun show(ui: DehumUi) = rule.setContent {
        CoolerTheme {
            HumidityScreen(
                ui, nowEpochS = 1_790_700_000L, onMode = { modes += it }, onStep = { _, _ -> },
                onEnter = { k, v -> entered += k to v },
            )
        }
    }

    @Test
    fun graphShowsTheReadingAndMode() {
        show(base)
        rule.onNodeWithText("3.4", substring = true).assertExists()
        rule.onNodeWithText("54", substring = true).assertExists()
        rule.onNodeWithText("CYCLE run 12 m left", substring = true).assertExists()
        rule.onNodeWithText("Relay 2 on", substring = true).assertExists()
        listOf("Temp", "RH", "Avg", "RH avg").forEach { rule.onNodeWithText(it).assertExists() }
    }

    @Test
    fun controlsShowFiveModesWithTheCurrentOneSelected() {
        show(base)
        rule.onNodeWithText("Controls").performClick()
        listOf("on", "off", "timer", "auto", "cycle").forEach { rule.onNodeWithTag("mode-$it").assertExists() }
        rule.onNodeWithTag("mode-cycle").assertIsSelected()
        rule.onNodeWithTag("mode-timer").performClick()
        assertEquals(listOf(DehumMode.TIMER), modes)
        rule.onNodeWithTag("value-dehum_rh").assertDoesNotExist()
        (1..4).forEach { rule.onNodeWithTag("rule-$it").performScrollTo().assertExists() }
    }

    @Test
    fun ruleCardsShowAndSendEachField() {
        show(base)
        rule.onNodeWithText("Controls").performClick()
        rule.onNodeWithTag("r3_sensor-0").performScrollTo().assertIsSelected()
        rule.onNodeWithTag("r3_relay-2").performScrollTo().assertIsSelected()
        rule.onNodeWithTag("rule-3-met").assertExists()
        rule.onNodeWithTag("rule-1-met").assertDoesNotExist()
        rule.onNodeWithTag("r2_en").performScrollTo().performClick()
        rule.onNodeWithTag("r2_cmp-1").performScrollTo().performClick()
        rule.onNodeWithTag("r4_action-0").performScrollTo().performClick()
        assertEquals(listOf("r2_en" to 1, "r2_cmp" to 1, "r4_action" to 0), entered)
    }

    @Test
    fun offlineDisablesTheControls() {
        show(base.copy(view = DehumView.OFFLINE))
        rule.onNodeWithText("Controls").performClick()
        rule.onNodeWithTag("mode-on").assertIsNotEnabled()
        rule.onNodeWithText("Controls work only while the dehumidifier is online.").assertExists()
    }
}
