package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.BrokerConfig
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class SetupScreenTest {
    @get:Rule val rule = createComposeRule()
    private var saved: BrokerConfig? = null

    private fun show(setup: SetupUi = SetupUi(), onCancel: (() -> Unit)? = null) = rule.setContent {
        CoolerTheme {
            SetupScreen(BrokerConfig(host = "mqtt.example.com", username = "app-user"), setup, onTestAndSave = { saved = it }, onCancel = onCancel)
        }
    }

    @Test
    fun testAndSaveNeedsAPassword() {
        show()
        rule.onNodeWithText("Test & save").assertIsNotEnabled()
        rule.onNodeWithText("Password").performTextInput("secret")
        rule.onNodeWithText("Test & save").assertIsEnabled().performClick()
        assertEquals(BrokerConfig(host = "mqtt.example.com", username = "app-user", password = "secret"), saved)
    }

    @Test
    fun showsTheProbeError() {
        show(SetupUi(error = "Can't reach the broker"))
        rule.onNodeWithText("Can't reach the broker").assertExists()
    }

    @Test
    fun buttonIsBusyWhileTesting() {
        show(SetupUi(probing = true))
        rule.onNodeWithText("Testing…").assertIsNotEnabled()
    }

    @Test
    fun cancelIsDisabledWhileTesting() {
        show(SetupUi(probing = true), onCancel = {})
        rule.onNodeWithText("Cancel").assertIsNotEnabled()
    }

    @Test
    fun cancelWorksWhenIdle() {
        show(onCancel = {})
        rule.onNodeWithText("Cancel").assertIsEnabled()
    }
}
