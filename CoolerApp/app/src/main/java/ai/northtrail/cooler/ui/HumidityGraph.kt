package ai.northtrail.cooler.ui

import ai.northtrail.cooler.DehumUi
import ai.northtrail.cooler.model.DehumStatusText
import ai.northtrail.cooler.model.SeriesShown
import ai.northtrail.cooler.model.TrendAverage
import ai.northtrail.cooler.model.TrendWindow
import ai.northtrail.cooler.model.TrendZoom
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
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
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

/** Humidity now and its 1-hour average, the mode, and the trend (like the Cooler's Status page). */
@Composable
fun HumidityGraph(ui: DehumUi, nowEpochS: Long) {
    val s = ui.state
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.Bottom) {
            val avg = TrendAverage.text(TrendAverage.latest(ui.trend, nowEpochS))
            Text(
                buildAnnotatedString {
                    append(s?.humidity?.roundToInt()?.toString() ?: "--")
                    withStyle(SpanStyle(color = CoolerColors.Average)) { append(" / $avg") }
                    withStyle(SpanStyle(fontSize = MaterialTheme.typography.headlineMedium.fontSize)) { append(" %RH") }
                },
                style = MaterialTheme.typography.displayLarge,
                maxLines = 1,
            )
        }
        Text(
            listOf(
                DehumStatusText.mode(s),
                if (s?.relay == true) "Running" else "Stopped",
                DehumStatusText.auto(s),
            ).filter { it.isNotEmpty() }.joinToString("  ·  "),
            color = CoolerColors.Muted,
            modifier = Modifier.padding(horizontal = 16.dp),
        )
        if (s?.linkLost == true) {
            Text("Broker link lost: off", color = CoolerColors.Warn, modifier = Modifier.padding(horizontal = 16.dp))
        }
        var zoom by rememberSaveable { mutableStateOf(TrendZoom.DEFAULT) }
        var showAvg by rememberSaveable { mutableStateOf(true) }
        val window = TrendWindow.of(zoom, nowEpochS)
        val withLead = ui.trend.samples(window.fromEpochS - TrendAverage.WINDOW_S)
        TrendChart(
            samples = ui.trend.samples(window.fromEpochS),
            fromEpochS = window.fromEpochS,
            toEpochS = window.toEpochS,
            windowLabel = window.label,
            setpoint = null,
            range = null,
            shown = SeriesShown(temp = true, rh = false, avg = showAvg),
            average = TrendAverage.rolling(withLead, window.fromEpochS),
            windowMean = TrendAverage.mean(withLead, window.fromEpochS, window.toEpochS),
            unit = "%",
            lineColor = CoolerColors.Humidity,
            threshold = s?.settings?.get("dehum_rh")?.toDouble(),
            modifier = Modifier.fillMaxWidth().height(220.dp).padding(horizontal = 16.dp).padding(top = 16.dp),
        )
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            TrendZoom.entries.forEach { z -> ToggleChip(z.label, on = z == zoom, onColor = CoolerColors.Accent) { zoom = z } }
            Spacer(Modifier.weight(1f))
            ToggleChip("Avg", on = showAvg, onColor = CoolerColors.Average) { showAvg = !showAvg }
        }
        Text(
            "Dashed line: AUTO threshold. Shaded: dehumidifier running.",
            color = CoolerColors.Muted,
            style = MaterialTheme.typography.labelSmall,
            modifier = Modifier.padding(horizontal = 16.dp),
        )
    }
}
