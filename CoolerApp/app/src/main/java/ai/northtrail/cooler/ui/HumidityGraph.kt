package ai.northtrail.cooler.ui

import ai.northtrail.cooler.DehumUi
import ai.northtrail.cooler.model.DehumBounds
import ai.northtrail.cooler.model.DehumStatusText
import ai.northtrail.cooler.model.Series
import ai.northtrail.cooler.model.SeriesShown
import ai.northtrail.cooler.model.StatusText
import ai.northtrail.cooler.model.TrendAverage
import ai.northtrail.cooler.model.TrendWindow
import ai.northtrail.cooler.model.TrendZoom
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
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

/**
 * The box sensor now and its 1-hour averages, the mode, and the trend of temperature and
 * humidity with both averages (like the Cooler's Status page). Spec 2026-10-31.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun HumidityGraph(ui: DehumUi, nowEpochS: Long) {
    val s = ui.state
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.Bottom) {
            val avg = TrendAverage.text(TrendAverage.latest(ui.trend, nowEpochS))
            Text(
                buildAnnotatedString {
                    append(StatusText.temp(s?.temp))
                    withStyle(SpanStyle(color = CoolerColors.Average)) { append(" / $avg") }
                    withStyle(SpanStyle(fontSize = MaterialTheme.typography.headlineMedium.fontSize)) { append(" °C") }
                },
                style = MaterialTheme.typography.displayMedium,
                maxLines = 1,
            )
        }
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), verticalAlignment = Alignment.Bottom) {
            val rhAvg = TrendAverage.text(TrendAverage.latestHumidity(ui.trend, nowEpochS))
            Text(
                buildAnnotatedString {
                    withStyle(SpanStyle(color = CoolerColors.Humidity)) { append(s?.humidity?.roundToInt()?.toString() ?: "--") }
                    withStyle(SpanStyle(color = CoolerColors.RhAverage)) { append(" / $rhAvg") }
                    withStyle(SpanStyle(fontSize = MaterialTheme.typography.titleMedium.fontSize)) { append(" %RH") }
                },
                style = MaterialTheme.typography.headlineLarge,
                maxLines = 1,
            )
        }
        Text(
            listOf(DehumStatusText.mode(s), DehumStatusText.relays(s), DehumStatusText.auto(s))
                .filter { it.isNotEmpty() }.joinToString("  ·  "),
            color = CoolerColors.Muted,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )
        if (s?.linkLost == true) {
            Text("Broker link lost: off", color = CoolerColors.Warn, modifier = Modifier.padding(horizontal = 16.dp))
        }
        var zoom by rememberSaveable { mutableStateOf(TrendZoom.DEFAULT) }
        var showTemp by rememberSaveable { mutableStateOf(true) }
        var showRh by rememberSaveable { mutableStateOf(true) }
        var showAvg by rememberSaveable { mutableStateOf(true) }
        var showRhAvg by rememberSaveable { mutableStateOf(true) }
        val shown = SeriesShown(showTemp, showRh, showAvg, showRhAvg)
        val window = TrendWindow.of(zoom, nowEpochS)
        val withLead = ui.trend.samples(window.fromEpochS - TrendAverage.WINDOW_S)
        val rhLead = TrendAverage.humidity(withLead)
        // A dashed line at each enabled rule's threshold, on its sensor's scale.
        val rules = (1..DehumBounds.RULE_COUNT).filter { s?.settings?.get(DehumBounds.ruleKey(it, "en")) == 1 }
        fun thresholdsFor(sensor: Int) = rules
            .filter { s?.settings?.get(DehumBounds.ruleKey(it, "sensor")) == sensor }
            .mapNotNull { s?.settings?.get(DehumBounds.ruleKey(it, "value"))?.toDouble() }
        TrendChart(
            samples = ui.trend.samples(window.fromEpochS),
            fromEpochS = window.fromEpochS,
            toEpochS = window.toEpochS,
            windowLabel = window.label,
            setpoint = null,
            range = null,
            shown = shown,
            average = TrendAverage.rolling(withLead, window.fromEpochS),
            windowMean = TrendAverage.mean(withLead, window.fromEpochS, window.toEpochS),
            rhAverage = TrendAverage.rolling(rhLead, window.fromEpochS),
            rhWindowMean = TrendAverage.mean(rhLead, window.fromEpochS, window.toEpochS),
            thresholds = thresholdsFor(DehumBounds.SENSOR_TEMP),
            rhThresholds = thresholdsFor(DehumBounds.SENSOR_HUM),
            modifier = Modifier.fillMaxWidth().height(240.dp).padding(horizontal = 16.dp).padding(top = 16.dp),
        )
        FlowRow(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            TrendZoom.entries.forEach { z -> ToggleChip(z.label, on = z == zoom, onColor = CoolerColors.Accent) { zoom = z } }
        }
        FlowRow(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            fun apply(next: SeriesShown) { showTemp = next.temp; showRh = next.rh; showAvg = next.avg; showRhAvg = next.rhAvg }
            ToggleChip("Temp", on = showTemp, onColor = CoolerColors.Accent) { apply(shown.toggled(Series.TEMP)) }
            ToggleChip("RH", on = showRh, onColor = CoolerColors.Humidity) { apply(shown.toggled(Series.RH)) }
            ToggleChip("Avg", on = showAvg, onColor = CoolerColors.Average) { apply(shown.toggled(Series.AVG)) }
            ToggleChip("RH avg", on = showRhAvg, onColor = CoolerColors.RhAverage) { apply(shown.toggled(Series.RH_AVG)) }
        }
        Text(
            "Dashed lines: enabled rules' thresholds. Shaded: relay 1 (dehumidifier) running.",
            color = CoolerColors.Muted,
            style = MaterialTheme.typography.labelSmall,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        )
    }
}
