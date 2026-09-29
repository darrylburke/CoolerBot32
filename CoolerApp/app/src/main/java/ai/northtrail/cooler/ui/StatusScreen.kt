package ai.northtrail.cooler.ui

import ai.northtrail.cooler.UiState
import ai.northtrail.cooler.model.Chip
import ai.northtrail.cooler.model.StatusText
import ai.northtrail.cooler.model.TrendWindow
import androidx.compose.foundation.background
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
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
        val window = TrendWindow.of(ui.trend.samples(nowEpochS - TrendWindow.MAX_SPAN_S), nowEpochS)
        TrendChart(
            samples = ui.trend.samples(window.fromEpochS),
            fromEpochS = window.fromEpochS,
            toEpochS = window.toEpochS,
            windowLabel = window.label,
            setpoint = s?.settings?.get("coolerset"),
            range = s?.settings?.get("range"),
            modifier = Modifier.fillMaxWidth().height(220.dp).padding(16.dp),
        )
    }
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
