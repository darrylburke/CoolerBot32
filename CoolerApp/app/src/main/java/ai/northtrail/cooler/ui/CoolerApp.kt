package ai.northtrail.cooler.ui

import ai.northtrail.cooler.CoolerViewModel
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
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.lifecycle.compose.collectAsStateWithLifecycle

private data class Tab(val label: String, val icon: ImageVector)

private val tabs = listOf(
    Tab("Status", Icons.Filled.AcUnit),
    Tab("Settings", Icons.Filled.Tune),
    Tab("Detail", Icons.Filled.Info),
)

@Composable
fun CoolerApp(viewModel: CoolerViewModel) {
    val ui by viewModel.ui.collectAsStateWithLifecycle()
    val setup by viewModel.setup.collectAsStateWithLifecycle()
    val brokerConfig by viewModel.brokerConfig.collectAsStateWithLifecycle()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var editingBroker by rememberSaveable { mutableStateOf(false) }
    val snackbar = remember { SnackbarHostState() }

    LaunchedEffect(ui.message) {
        ui.message?.let {
            snackbar.showSnackbar(it)
            viewModel.consumeMessage()
        }
    }

    if (!ui.configured || editingBroker) {
        SetupScreen(
            initial = brokerConfig,
            setup = setup,
            onTestAndSave = { viewModel.testAndSave(it) { editingBroker = false } },
            onCancel = if (ui.configured) ({ editingBroker = false }) else null,
        )
        return
    }

    // ui changes at least once a second (the controller's ticker), so this stays current.
    val nowEpochS = remember(ui) { System.currentTimeMillis() / 1_000 }
    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = {
            NavigationBar {
                tabs.forEachIndexed { i, t ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = { tab = i },
                        icon = { Icon(t.icon, contentDescription = null) },
                        label = { Text(t.label) },
                    )
                }
            }
        },
    ) { padding ->
        Box(Modifier.padding(padding)) {
            when (tab) {
                0 -> StatusScreen(ui, nowEpochS)
                1 -> SettingsScreen(ui, viewModel::stepSetting, viewModel::chooseSetting)
                else -> DetailScreen(
                    ui = ui,
                    brokerHost = brokerConfig.host,
                    onStartCal = viewModel::startCalibration,
                    onAbortCal = viewModel::abortCalibration,
                    onResetCal = viewModel::resetFinCal,
                    onBrokerSettings = { editingBroker = true },
                )
            }
        }
    }
}
