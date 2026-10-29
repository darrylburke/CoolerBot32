package ai.northtrail.cooler.ui

import ai.northtrail.cooler.DehumUi
import ai.northtrail.cooler.model.DehumBounds
import ai.northtrail.cooler.model.DehumMode
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp

/** Mode buttons and the five dehumidifier settings, each − / value / + with typed entry. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun HumidityControls(
    ui: DehumUi,
    onMode: (DehumMode) -> Unit,
    onStep: (String, Int) -> Unit,
    onEnter: (String, Int) -> Unit,
) {
    val enabled = ui.controlsEnabled
    var openKey by rememberSaveable { mutableStateOf<String?>(null) }
    val focusManager = LocalFocusManager.current
    Column(
        Modifier.fillMaxSize()
            .pointerInput(Unit) { detectTapGestures { focusManager.clearFocus(); openKey = null } }
            .verticalScroll(rememberScrollState()),
    ) {
        if (!enabled) {
            Text(
                "Controls work only while the dehumidifier is online.",
                color = CoolerColors.Muted,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }
        Text(
            "Mode",
            style = MaterialTheme.typography.titleMedium,
            color = CoolerColors.Accent,
            modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 4.dp),
        )
        FlowRow(Modifier.padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            DehumMode.BUTTONS.forEach { m ->
                FilterChip(
                    selected = ui.state?.mode == m,
                    onClick = { onMode(m) },
                    enabled = enabled,
                    label = { Text(m.label) },
                    modifier = Modifier.testTag("mode-${m.wire}"),
                )
            }
        }
        Text(
            "Settings",
            style = MaterialTheme.typography.titleMedium,
            color = CoolerColors.Accent,
            modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 4.dp),
        )
        val minRun = ui.settingValue("min_run") ?: 0
        DehumBounds.ALL.forEach { b ->
            val value = ui.settingValue(b.key)
            SteppedValue(
                label = b.label,
                valueText = DehumBounds.format(b.key, value),
                pending = ui.isPending(b.key),
                lo = DehumBounds.floor(b.key, minRun),
                hi = b.hi,
                value = value,
                enabled = enabled,
                open = openKey == b.key,
                onOpen = { openKey = b.key },
                onClose = { if (openKey == b.key) openKey = null },
                onSend = { onEnter(b.key, it) },
                onStep = { onStep(b.key, it) },
                tag = b.key,
                note = if (b.key == "timer_min" && ui.state?.mode == DehumMode.TIMER) {
                    "Lowering below the time already run ends the timer."
                } else {
                    null
                },
            )
        }
    }
}
