package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.AxisTicks
import ai.northtrail.cooler.model.Sample
import ai.northtrail.cooler.model.SeriesShown
import ai.northtrail.cooler.model.StatusText
import ai.northtrail.cooler.model.TrendSegments
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp

/**
 * Box temperature and humidity from [fromEpochS] to [toEpochS]. Temperature: the
 * setpoint band shaded, spans with the relay closed tinted, a line, and whole-degree
 * labels on the left. Humidity: a thinner line on its own scale, labelled on the
 * right. [average] is the 1-hour rolling mean (see [TrendAverage]) drawn over the
 * temperature, with [windowMean] as "avg" in the header. [shown] hides any of them.
 * Lines break where samples are missing (see [TrendSegments]).
 */
@Composable
fun TrendChart(
    samples: List<Sample>,
    fromEpochS: Long,
    toEpochS: Long,
    windowLabel: String,
    setpoint: Int?,
    range: Int?,
    shown: SeriesShown,
    average: List<Pair<Long, Double>> = emptyList(),
    windowMean: Double? = null,
    modifier: Modifier = Modifier,
) {
    if (samples.size < 2) {
        Box(modifier.background(CoolerColors.Surface, RoundedCornerShape(12.dp)), contentAlignment = Alignment.Center) {
            Text("No history yet", color = CoolerColors.Muted)
        }
        return
    }
    val bandLo = if (setpoint != null && range != null) (setpoint - range).toDouble() else null
    val bandHi = if (setpoint != null && range != null) (setpoint + range).toDouble() else null
    val temps = samples.map { it.tempC }
    val yMin = listOfNotNull(temps.min(), bandLo).min() - 1.0
    val yMax = listOfNotNull(temps.max(), bandHi).max() + 1.0
    val hums = samples.mapNotNull { it.humidity }
    val showRh = shown.rh && hums.isNotEmpty()
    val hMin = (hums.minOrNull() ?: 0.0) - 2.0
    val hMax = (hums.maxOrNull() ?: 0.0) + 2.0
    val span = (toEpochS - fromEpochS).coerceAtLeast(1).toFloat()
    val segments = TrendSegments.split(samples)
    // The average line is drawn on the temperature scale too.
    val tempScale = shown.temp || shown.avg
    val tempTicks = if (tempScale) AxisTicks.of(yMin, yMax) else emptyList()
    val rhTicks = if (showRh) AxisTicks.of(hMin, hMax) else emptyList()
    val measurer = rememberTextMeasurer()
    val tickStyle = MaterialTheme.typography.labelSmall

    Column(modifier) {
        Row(Modifier.fillMaxWidth()) {
            if (shown.temp) {
                Text("max ${StatusText.temp(temps.max())}°", color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
            }
            if (shown.avg && windowMean != null) {
                Text(
                    "   avg ${StatusText.temp(windowMean)}°",
                    color = CoolerColors.Average,
                    style = MaterialTheme.typography.labelSmall,
                )
            }
            Spacer(Modifier.weight(1f))
            Text(windowLabel, color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
        }
        Canvas(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(vertical = 4.dp)
                .background(CoolerColors.Surface, RoundedCornerShape(12.dp)),
        ) {
            // Each shown scale gets a gutter of its own; the plot sits between them.
            val left = if (tempScale) 28.dp.toPx() else 4.dp.toPx()
            val right = size.width - (if (showRh) 32.dp.toPx() else 4.dp.toPx())
            val plotW = right - left
            fun x(t: Long) = left + (t - fromEpochS) / span * plotW
            fun y(c: Double) = ((yMax - c) / (yMax - yMin)).toFloat() * size.height
            fun hy(h: Double) = ((hMax - h) / (hMax - hMin)).toFloat() * size.height

            tempTicks.forEach { v ->
                drawLine(CoolerColors.Muted.copy(alpha = 0.25f), Offset(left, y(v)), Offset(right, y(v)), 1.dp.toPx())
                tick(measurer, "%.0f".format(v), tickStyle.copy(color = CoolerColors.Muted), left - 4.dp.toPx(), y(v), alignRight = true)
            }
            rhTicks.forEach { v ->
                tick(measurer, "%.0f".format(v), tickStyle.copy(color = CoolerColors.Humidity), right + 4.dp.toPx(), hy(v), alignRight = false)
            }
            if (shown.temp) {
                segments.forEach { seg ->
                    seg.zipWithNext().forEach { (a, b) ->
                        if (a.relay == true) {
                            drawRect(CoolerColors.RelayTint, Offset(x(a.epochS), 0f), Size(x(b.epochS) - x(a.epochS), size.height))
                        }
                    }
                }
                if (bandLo != null && bandHi != null) {
                    drawRect(CoolerColors.Band, Offset(left, y(bandHi)), Size(plotW, y(bandLo) - y(bandHi)))
                }
            }
            if (showRh) {
                // A reading without humidity breaks the line, as a missing sample does.
                val rhRuns = segments.flatMap { seg -> splitWhere(seg) { it.humidity == null } }
                trace(rhRuns, CoolerColors.Humidity, 1.5f, { x(it.epochS) }, { hy(it.humidity!!) })
            }
            if (shown.temp) trace(segments, CoolerColors.Accent, 2f, { x(it.epochS) }, { y(it.tempC) })
            if (shown.avg && average.size >= 2) {
                // The rolling mean is smooth by construction; one path, broken only
                // where the record (and so the mean) has a hole.
                val line = Path()
                var prev: Long? = null
                average.forEach { (t, v) ->
                    val px = x(t).coerceAtLeast(left)
                    if (prev == null || t - prev!! > TrendSegments.MIN_GAP_S) line.moveTo(px, y(v)) else line.lineTo(px, y(v))
                    prev = t
                }
                drawPath(line, CoolerColors.Average, style = Stroke(width = 2.dp.toPx()))
            }
        }
        if (shown.temp) {
            Text("min ${StatusText.temp(temps.min())}°", color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
        }
    }
}

/** Runs of [items] between the ones [isGap] marks; the marked ones are dropped. */
private fun <T> splitWhere(items: List<T>, isGap: (T) -> Boolean): List<List<T>> {
    val out = mutableListOf<List<T>>()
    var run = mutableListOf<T>()
    for (i in items) {
        if (isGap(i)) {
            if (run.isNotEmpty()) out += run
            run = mutableListOf()
        } else {
            run += i
        }
    }
    if (run.isNotEmpty()) out += run
    return out
}

/** One line per run; a lone reading between gaps is a dot. */
private fun DrawScope.trace(
    runs: List<List<Sample>>,
    color: Color,
    widthDp: Float,
    px: (Sample) -> Float,
    py: (Sample) -> Float,
) {
    val line = Path()
    runs.forEach { run ->
        run.forEachIndexed { i, s -> if (i == 0) line.moveTo(px(s), py(s)) else line.lineTo(px(s), py(s)) }
        if (run.size == 1) drawCircle(color, widthDp.dp.toPx(), Offset(px(run[0]), py(run[0])))
    }
    drawPath(line, color, style = Stroke(width = widthDp.dp.toPx()))
}

private fun DrawScope.tick(m: TextMeasurer, text: String, style: TextStyle, x: Float, y: Float, alignRight: Boolean) {
    val label = m.measure(text, style)
    val left = if (alignRight) x - label.size.width else x
    drawText(label, topLeft = Offset(left, (y - label.size.height / 2f).coerceIn(0f, size.height - label.size.height)))
}
