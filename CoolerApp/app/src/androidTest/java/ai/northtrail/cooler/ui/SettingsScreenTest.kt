package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import ai.northtrail.cooler.model.Pending
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class SettingsScreenTest {
    @get:Rule val rule = createComposeRule()
    private val steps = mutableListOf<Pair<String, Int>>()
    private val choices = mutableListOf<Pair<String, Int>>()

    private val online = Health(LinkView.ONLINE, "Online", emptyList(), 1_000L)
    private val settings = mapOf(
        "coolerset" to 4, "range" to 2, "sampleinterval" to 3600, "fin_cutoff" to 3, "fin_recover" to 4,
        "settle" to 10, "minofftime" to 5, "minruntime" to 180, "maxrun" to 10, "dutypercent" to 50,
    )
    private val base = UiState(configured = true, state = CoolerState(settings = settings), health = online)

    private fun show(ui: UiState) = rule.setContent {
        CoolerTheme { SettingsScreen(ui, onStep = { k, d -> steps += k to d }, onChoose = { k, v -> choices += k to v }) }
    }

    @Test
    fun controlsAreOffUnlessOnline() {
        show(base.copy(health = Health(LinkView.UNREACHABLE, "Broker unreachable", emptyList(), null)))
        rule.onNodeWithContentDescription("Raise Set point").assertIsNotEnabled()
        rule.onNodeWithText("Settings can only be changed while the controller is online.").assertExists()
    }

    @Test
    fun steppersReportTheirKeyAndDirection() {
        show(base)
        rule.onNodeWithContentDescription("Raise Set point").assertIsEnabled().performClick()
        rule.onNodeWithContentDescription("Lower Set point").performClick()
        assertEquals(listOf("coolerset" to 1, "coolerset" to -1), steps)
    }

    @Test
    fun pendingShowsTheSentValueWithASpinner() {
        show(base.copy(pending = mapOf("coolerset" to Pending(7, 0))))
        rule.onNodeWithTag("value-coolerset").assertTextEquals("7 °C")
        rule.onNodeWithTag("pending-coolerset").assertExists()
    }

    @Test
    fun confirmedShowsTheControllersValueWithoutASpinner() {
        show(base.copy(state = CoolerState(settings = settings + ("coolerset" to 7))))
        rule.onNodeWithTag("value-coolerset").assertTextEquals("7 °C")
        rule.onNodeWithTag("pending-coolerset").assertDoesNotExist()
    }

    @Test
    fun iceClearCannotBeLoweredToIceCutoff() {
        show(base)
        rule.onNodeWithContentDescription("Lower Ice clear").performScrollTo().assertIsNotEnabled()
        rule.onNodeWithContentDescription("Raise Ice clear").assertIsEnabled()
    }

    @Test
    fun presetsReportTheChosenInterval() {
        show(base)
        rule.onNodeWithTag("value-sampleinterval").assertTextEquals("1 h")
        rule.onNodeWithText("5 m").performClick()
        assertEquals(listOf("sampleinterval" to 300), choices)
    }
}
