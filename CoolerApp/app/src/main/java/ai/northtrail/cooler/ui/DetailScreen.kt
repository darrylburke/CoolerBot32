package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.DetailRows
import ai.northtrail.cooler.model.StatusText
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun DetailScreen(
    ui: UiState,
    brokerHost: String,
    onStartCal: () -> Unit,
    onAbortCal: () -> Unit,
    onResetCal: () -> Unit,
    onBrokerSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var confirmReset by remember { mutableStateOf(false) }
    val s = ui.state
    val enabled = ui.health.controlsEnabled

    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        StatusStrip(ui.health)
        DetailRows.of(s, ui.health).forEach { (label, value) ->
            Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp)) {
                Text(label, color = CoolerColors.Muted, modifier = Modifier.weight(1f))
                Text(value)
            }
        }

        Heading("Fin calibration")
        Text(StatusText.finCal(s), modifier = Modifier.padding(horizontal = 16.dp))
        Row(Modifier.padding(16.dp)) {
            if (s?.calActive == true) {
                OutlinedButton(onClick = onAbortCal, enabled = enabled) { Text("Abort calibration") }
            } else {
                Button(onClick = onStartCal, enabled = enabled) { Text("Start calibration") }
            }
            Spacer(Modifier.width(8.dp))
            OutlinedButton(onClick = { confirmReset = true }, enabled = enabled) { Text("Reset calibration") }
        }

        Heading("Broker")
        Text(brokerHost, color = CoolerColors.Muted, modifier = Modifier.padding(horizontal = 16.dp))
        OutlinedButton(onClick = onBrokerSettings, modifier = Modifier.padding(16.dp)) { Text("Broker settings") }
    }

    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Reset fin calibration?") },
            text = { Text("The controller forgets the calibration it collected and goes back to the default thermistor curve.") },
            confirmButton = {
                TextButton(onClick = { confirmReset = false; onResetCal() }) { Text("Reset") }
            },
            dismissButton = {
                TextButton(onClick = { confirmReset = false }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun Heading(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.titleMedium,
        color = CoolerColors.Accent,
        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 4.dp),
    )
}
