package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import ai.northtrail.cooler.model.TrendBuffer
import android.graphics.Bitmap
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AcUnit
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onRoot
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * Renders each screen from fixed sample data and saves PNGs for the README
 * (docs/screenshots/). Skipped unless asked for:
 *
 *   ./gradlew connectedDebugAndroidTest \
 *     -Pandroid.testInstrumentationRunnerArguments.class=ai.northtrail.cooler.ui.ReadmeScreenshots \
 *     -Pandroid.testInstrumentationRunnerArguments.screenshots=true
 *   (leave the APKs installed: -Pandroid.injected.androidTest.leaveApksInstalledAfterRun=true)
 *   adb pull /sdcard/Android/data/ai.northtrail.cooler/files/screenshots/
 */
class ReadmeScreenshots {
    @get:Rule val rule = createComposeRule()

    private val now = 1_790_640_000L
    private val online = Health(LinkView.ONLINE, "Online", emptyList(), 12_000L)
    private val settings = mapOf(
        "coolerset" to 4, "range" to 2, "sampleinterval" to 3600, "fin_cutoff" to 1, "fin_recover" to 3,
        "settle" to 10, "minofftime" to 5, "minruntime" to 180, "maxrun" to 10, "dutypercent" to 50,
    )
    private val cooling = CoolerState(
        temp = 4.8, humidity = 78.2, finTemp = 1.6, finOhms = 28410.0, finSlope = -0.8,
        mode = "normal", overrideSrc = "none", state = "cooling", relay = true, coolCall = true,
        compressor = 1, runS = 142, holdS = 38, settings = settings, finCal = true, uptimeS = 86_400,
    )

    /** Six hours of a box cycling between its on (6 °C) and off (2 °C) points. */
    private fun trend(): TrendBuffer {
        var b = TrendBuffer()
        var t = 5.5
        var relay = false
        for (m in 360 downTo 0) {
            if (relay) t -= 0.09 else t += 0.035
            if (t > 6.0) relay = true
            if (t < 2.0) relay = false
            b = b.appended(now - m * 60L, t, relay)
        }
        return b
    }

    private val base = UiState(configured = true, state = cooling, health = online, trend = trend())

    @Before fun onlyWhenAsked() {
        val args = InstrumentationRegistry.getArguments()
        assumeTrue(args.getString("screenshots") == "true")
    }

    @Composable
    private fun Frame(selected: Int, content: @Composable () -> Unit) = CoolerTheme {
        Scaffold(
            bottomBar = {
                NavigationBar {
                    listOf("Status" to Icons.Filled.AcUnit, "Settings" to Icons.Filled.Tune, "Detail" to Icons.Filled.Info)
                        .forEachIndexed { i, (label, icon) ->
                            NavigationBarItem(
                                selected = i == selected, onClick = {},
                                icon = { Icon(icon, contentDescription = null) }, label = { Text(label) },
                            )
                        }
                }
            },
        ) { padding -> Box(Modifier.padding(padding)) { content() } }
    }

    private fun save(name: String) {
        rule.waitForIdle()
        val bmp = rule.onRoot().captureToImage().asAndroidBitmap()
        val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.getExternalFilesDir(null), "screenshots")
        dir.mkdirs()
        File(dir, "$name.png").outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test fun status() {
        rule.setContent { Frame(0) { StatusScreen(base, now) } }
        save("app-status")
    }

    @Test fun statusDefrost() {
        val s = cooling.copy(state = "defrost", relay = false, coolCall = true, defrost = true, finTemp = 0.6, offS = 75, holdS = 0, compressor = 0)
        rule.setContent { Frame(0) { StatusScreen(base.copy(state = s), now) } }
        save("app-status-defrost")
    }

    @Test fun settings() {
        rule.setContent { Frame(1) { SettingsScreen(base, { _, _ -> }, { _, _ -> }) } }
        save("app-settings")
    }

    @Test fun detail() {
        rule.setContent { Frame(2) { DetailScreen(base, "mqtt.example.com", {}, {}, {}, {}) } }
        save("app-detail")
    }

    @Test fun setup() {
        rule.setContent {
            CoolerTheme { SetupScreen(BrokerConfig(host = "mqtt.example.com", username = "app-user"), SetupUi(), {}, null) }
        }
        save("app-setup")
    }
}
