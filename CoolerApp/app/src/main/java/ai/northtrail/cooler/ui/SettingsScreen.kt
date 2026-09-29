package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.Bound
import ai.northtrail.cooler.model.Bounds
import ai.northtrail.cooler.model.Group
import ai.northtrail.cooler.model.Widget
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp

@Composable
fun SettingsScreen(
    ui: UiState,
    onStep: (String, Int) -> Unit,
    onChoose: (String, Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val enabled = ui.health.controlsEnabled
    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        StatusStrip(ui.health)
        if (!enabled) {
            Text(
                "Settings can only be changed while the controller is online.",
                color = CoolerColors.Muted,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }
        Group.entries.forEach { group ->
            Text(
                group.title,
                style = MaterialTheme.typography.titleMedium,
                color = CoolerColors.Accent,
                modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 4.dp),
            )
            Bounds.ALL.filter { it.group == group }.forEach { b ->
                if (b.widget == Widget.PRESET) PresetRow(b, ui, enabled, onChoose) else StepperRow(b, ui, enabled, onStep)
            }
        }
    }
}

@Composable
private fun ValueText(b: Bound, ui: UiState) {
    val pending = ui.isPending(b.key)
    if (pending) CircularProgressIndicator(Modifier.size(16.dp).testTag("pending-${b.key}"), strokeWidth = 2.dp)
    Text(
        Bounds.format(b.key, ui.settingValue(b.key)),
        color = if (pending) CoolerColors.Muted else CoolerColors.Text,
        modifier = Modifier.padding(horizontal = 12.dp).testTag("value-${b.key}"),
    )
}

@Composable
private fun StepperRow(b: Bound, ui: UiState, enabled: Boolean, onStep: (String, Int) -> Unit) {
    val value = ui.settingValue(b.key)
    val floor = Bounds.floor(b.key, ui.settingValue("fin_cutoff") ?: b.lo)
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp).heightIn(min = 56.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(b.label, Modifier.weight(1f))
        ValueText(b, ui)
        IconButton(onClick = { onStep(b.key, -1) }, enabled = enabled && value != null && value > floor) {
            Icon(Icons.Filled.Remove, contentDescription = "Lower ${b.label}")
        }
        IconButton(onClick = { onStep(b.key, +1) }, enabled = enabled && value != null && value < b.hi) {
            Icon(Icons.Filled.Add, contentDescription = "Raise ${b.label}")
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PresetRow(b: Bound, ui: UiState, enabled: Boolean, onChoose: (String, Int) -> Unit) {
    val value = ui.settingValue(b.key)
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)) {
        Row(Modifier.heightIn(min = 56.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(b.label, Modifier.weight(1f))
            ValueText(b, ui)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Bounds.SAMPLE_PRESETS.forEach { (seconds, label) ->
                FilterChip(
                    selected = value == seconds,
                    onClick = { onChoose(b.key, seconds) },
                    enabled = enabled,
                    label = { Text(label) },
                )
            }
        }
    }
}
