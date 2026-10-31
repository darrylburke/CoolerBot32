package ai.northtrail.cooler.ui

import ai.northtrail.cooler.DehumUi
import ai.northtrail.cooler.model.DehumBounds
import ai.northtrail.cooler.model.DehumMode
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Switch
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp

/**
 * Mode buttons, the four timing settings (each − / value / + with typed entry), and AUTO's
 * four rules (spec 2026-10-31-dehum-rules-and-temp-humidity-tab-design.md).
 */
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
        DehumBounds.ALL.forEach { b ->
            val value = ui.settingValue(b.key)
            val range = DehumBounds.range(b.key, ui::settingValue)!!
            SteppedValue(
                label = b.label,
                valueText = DehumBounds.format(b.key, value),
                pending = ui.isPending(b.key),
                lo = range.first,
                hi = range.last,
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
        Text(
            "Rules",
            style = MaterialTheme.typography.titleMedium,
            color = CoolerColors.Accent,
            modifier = Modifier.padding(start = 16.dp, top = 16.dp),
        )
        Text(
            "Rules apply only in AUTO. Off wins when rules disagree; with no rule met the relay is off.",
            color = CoolerColors.Muted,
            style = MaterialTheme.typography.labelMedium,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )
        (1..DehumBounds.RULE_COUNT).forEach { n ->
            RuleCard(
                n = n,
                ui = ui,
                enabled = enabled,
                open = openKey == DehumBounds.ruleKey(n, "value"),
                onOpen = { openKey = DehumBounds.ruleKey(n, "value") },
                onClose = { if (openKey == DehumBounds.ruleKey(n, "value")) openKey = null },
                onStep = onStep,
                onEnter = onEnter,
            )
        }
        Spacer(Modifier.height(16.dp))
    }
}

/** One rule: Enabled · Temp | Humidity · Above | Below · value · Relay 1 | 2 · On | Off. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun RuleCard(
    n: Int,
    ui: DehumUi,
    enabled: Boolean,
    open: Boolean,
    onOpen: () -> Unit,
    onClose: () -> Unit,
    onStep: (String, Int) -> Unit,
    onEnter: (String, Int) -> Unit,
) {
    fun key(field: String) = DehumBounds.ruleKey(n, field)
    val on = ui.settingValue(key("en")) == 1
    val met = ui.state?.ruleMatch?.getOrNull(n - 1) == true
    Column(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 6.dp)
            .background(CoolerColors.Surface, RoundedCornerShape(16.dp))
            .padding(vertical = 8.dp)
            .testTag("rule-$n"),
    ) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Rule $n", style = MaterialTheme.typography.titleSmall)
            if (on && met) {
                Text(
                    "  met",
                    color = CoolerColors.Ok,
                    style = MaterialTheme.typography.labelMedium,
                    modifier = Modifier.testTag("rule-$n-met"),
                )
            }
            Spacer(Modifier.weight(1f))
            if (ui.isPending(key("en"))) {
                CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
            }
            Switch(
                checked = on,
                onCheckedChange = { onEnter(key("en"), if (it) 1 else 0) },
                enabled = enabled,
                modifier = Modifier.testTag(key("en")),
            )
        }
        Choice(ui, key("sensor"), listOf("Temp" to DehumBounds.SENSOR_TEMP, "Humidity" to DehumBounds.SENSOR_HUM), enabled, onEnter)
        Choice(ui, key("cmp"), listOf("Above" to 1, "Below" to 0), enabled, onEnter)
        val range = DehumBounds.range(key("value"), ui::settingValue)!!
        val value = ui.settingValue(key("value"))
        SteppedValue(
            label = "Threshold",
            valueText = DehumBounds.format(key("value"), value, ui::settingValue),
            pending = ui.isPending(key("value")),
            lo = range.first,
            hi = range.last,
            value = value,
            enabled = enabled,
            open = open,
            onOpen = onOpen,
            onClose = onClose,
            onSend = { onEnter(key("value"), it) },
            onStep = { onStep(key("value"), it) },
            tag = key("value"),
        )
        Choice(ui, key("relay"), listOf("Relay 1" to 1, "Relay 2" to 2), enabled, onEnter)
        Choice(ui, key("action"), listOf("Turn on" to 1, "Turn off" to 0), enabled, onEnter)
    }
}

/** A row of chips for one rule field; the selected chip follows drafts, then `dehum/data`. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Choice(ui: DehumUi, key: String, options: List<Pair<String, Int>>, enabled: Boolean, onEnter: (String, Int) -> Unit) {
    val current = ui.settingValue(key)
    FlowRow(
        Modifier.padding(horizontal = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        options.forEach { (label, v) ->
            FilterChip(
                selected = current == v,
                onClick = { if (current != v) onEnter(key, v) },
                enabled = enabled,
                label = { Text(label) },
                modifier = Modifier.testTag("$key-$v"),
            )
        }
        if (ui.isPending(key)) {
            CircularProgressIndicator(Modifier.size(16.dp).align(Alignment.CenterVertically), strokeWidth = 2.dp)
        }
    }
}
