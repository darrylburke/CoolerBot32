package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class DetailScreenTest {
    @get:Rule val rule = createComposeRule()
    private var starts = 0
    private var aborts = 0
    private var resets = 0
    private var brokerClicks = 0

    private val online = Health(LinkView.ONLINE, "Online", emptyList(), 0L)

    private fun show(ui: UiState) = rule.setContent {
        CoolerTheme {
            DetailScreen(
                ui, brokerHost = "mqtt.example.com",
                onStartCal = { starts++ }, onAbortCal = { aborts++ }, onResetCal = { resets++ },
                onBrokerSettings = { brokerClicks++ },
            )
        }
    }

    @Test
    fun resetNeedsConfirmation() {
        show(UiState(configured = true, state = CoolerState(finCal = true), health = online))
        rule.onNodeWithText("Reset calibration").performScrollTo().performClick()
        assertEquals(0, resets)
        rule.onNodeWithText("Reset fin calibration?").assertExists()
        rule.onNodeWithText("Reset").performClick()
        assertEquals(1, resets)
    }

    @Test
    fun startOrAbortFollowsTheCalibrationRun() {
        show(UiState(configured = true, state = CoolerState(calActive = true, calPoints = 3, calSpan = 6.2), health = online))
        rule.onNodeWithText("calibrating 3 pts 6.2C").performScrollTo().assertExists()
        rule.onNodeWithText("Abort calibration").performScrollTo().performClick()
        assertEquals(1, aborts)
        assertEquals(0, starts)
    }

    @Test
    fun calibrationIsOffUnlessOnline() {
        show(
            UiState(
                configured = true,
                state = CoolerState(),
                health = Health(LinkView.CONTROLLER_OFFLINE, "Controller offline", emptyList(), null),
            ),
        )
        rule.onNodeWithText("Start calibration").performScrollTo().assertIsNotEnabled()
        rule.onNodeWithText("Broker settings").performScrollTo().performClick()
        assertEquals(1, brokerClicks)
    }
}
