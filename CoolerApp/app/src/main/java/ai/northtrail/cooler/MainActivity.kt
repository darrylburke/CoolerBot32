package ai.northtrail.cooler

import ai.northtrail.cooler.ui.CoolerApp
import ai.northtrail.cooler.ui.CoolerTheme
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels

class MainActivity : ComponentActivity() {
    private val viewModel: CoolerViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { CoolerTheme { CoolerApp(viewModel) } }
    }

    // Connected only while visible: no background service (spec §7).
    override fun onStart() {
        super.onStart()
        viewModel.connect()
    }

    override fun onStop() {
        // Rotation stops and restarts the activity: keep the session across it.
        if (!isChangingConfigurations) viewModel.disconnect()
        super.onStop()
    }
}
