package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.Chip
import ai.northtrail.cooler.model.Series
import ai.northtrail.cooler.model.SeriesShown
import ai.northtrail.cooler.model.StatusText
import ai.northtrail.cooler.model.TrendWindow
import ai.northtrail.cooler.model.TrendZoom
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun StatusScreen(ui: UiState, nowEpochS: Long, modifier: Modifier = Modifier) {
    val s = ui.state
    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        StatusStrip(ui.health)
        Banners(ui.health.banners)
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.Bottom,
        ) {
            Text("${StatusText.temp(s?.temp)} °C", style = MaterialTheme.typography.displayLarge)
            Spacer(Modifier.weight(1f))
            Text(
                s?.humidity?.let { "${it.roundToInt()} %RH" } ?: "-- %RH",
                style = MaterialTheme.typography.titleLarge,
                color = CoolerColors.Muted,
            )
        }
        Text(StatusText.setpointLine(s), color = CoolerColors.Muted, modifier = Modifier.padding(horizontal = 16.dp))
        Column(
            Modifier
                .fillMaxWidth()
                .padding(16.dp)
                .background(CoolerColors.Surface, RoundedCornerShape(16.dp))
                .padding(16.dp),
        ) {
            Text(StatusText.stateKey(s), style = MaterialTheme.typography.labelLarge, color = CoolerColors.Accent)
            Text(StatusText.stateValue(s), style = MaterialTheme.typography.headlineSmall)
        }
        FlowRow(
            Modifier.padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            StatusText.chips(s).forEach { ChipView(it) }
        }
        Row(Modifier.fillMaxWidth().padding(16.dp)) {
            Text("Coil ${StatusText.temp(s?.finTemp)} °C", modifier = Modifier.weight(1f))
            Text("Compressor ${StatusText.compressor(s)}")
        }
        // View choices live with this screen only: like the panel, the app opens on
        // 3 h with both traces every time.
        var zoom by rememberSaveable { mutableStateOf(TrendZoom.DEFAULT) }
        var showTemp by rememberSaveable { mutableStateOf(true) }
        var showRh by rememberSaveable { mutableStateOf(true) }
        val shown = SeriesShown(showTemp, showRh)
        val window = TrendWindow.of(zoom, nowEpochS)
        TrendChart(
            samples = ui.trend.samples(window.fromEpochS),
            fromEpochS = window.fromEpochS,
            toEpochS = window.toEpochS,
            windowLabel = window.label,
            setpoint = s?.settings?.get("coolerset"),
            range = s?.settings?.get("range"),
            shown = shown,
            modifier = Modifier.fillMaxWidth().height(220.dp).padding(horizontal = 16.dp).padding(top = 16.dp),
        )
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            TrendZoom.entries.forEach { z ->
                ToggleChip(z.label, on = z == zoom, onColor = CoolerColors.Accent) { zoom = z }
            }
            Spacer(Modifier.weight(1f))
            fun apply(next: SeriesShown) { showTemp = next.temp; showRh = next.rh }
            ToggleChip("Temp", on = showTemp, onColor = CoolerColors.Accent) { apply(shown.toggled(Series.TEMP)) }
            ToggleChip("RH", on = showRh, onColor = CoolerColors.Humidity) { apply(shown.toggled(Series.RH)) }
        }
    }
}

/** A pill that reads as selected (filled, in [onColor]) or not (outlined, muted). */
@Composable
private fun ToggleChip(text: String, on: Boolean, onColor: Color, onClick: () -> Unit) {
    Text(
        text,
        color = if (on) onColor else CoolerColors.Muted,
        style = MaterialTheme.typography.labelLarge,
        modifier = Modifier
            .clip(RoundedCornerShape(16.dp))
            .background(if (on) CoolerColors.Surface else Color.Transparent)
            .border(1.dp, if (on) Color.Transparent else CoolerColors.Muted.copy(alpha = 0.5f), RoundedCornerShape(16.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 8.dp),
    )
}

@Composable
private fun ChipView(chip: Chip) {
    Text(
        chip.text,
        color = if (chip.alert) CoolerColors.Warn else CoolerColors.Muted,
        style = MaterialTheme.typography.labelMedium,
        modifier = Modifier
            .background(if (chip.alert) CoolerColors.WarnBackground else CoolerColors.Surface, RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp, vertical = 6.dp),
    )
}
