package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.AxisTicks
import ai.northtrail.cooler.model.Sample
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
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp

/**
 * Box temperature from [fromEpochS] to [toEpochS]: the setpoint band shaded,
 * spans with the relay closed tinted, the temperature as a line that breaks
 * where samples are missing (see [TrendSegments]), and whole-degree labels
 * with faint gridlines down the left.
 */
@Composable
fun TrendChart(
    samples: List<Sample>,
    fromEpochS: Long,
    toEpochS: Long,
    windowLabel: String,
    setpoint: Int?,
    range: Int?,
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
    val span = (toEpochS - fromEpochS).coerceAtLeast(1).toFloat()
    val segments = TrendSegments.split(samples)
    val ticks = AxisTicks.of(yMin, yMax)
    val measurer = rememberTextMeasurer()
    val tickStyle = MaterialTheme.typography.labelSmall.copy(color = CoolerColors.Muted)

    Column(modifier) {
        Row(Modifier.fillMaxWidth()) {
            Text("max ${StatusText.temp(temps.max())}°", color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
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
            // The labels get a gutter of their own; the plot starts after it.
            val gutter = 28.dp.toPx()
            val plotW = size.width - gutter
            fun x(t: Long) = gutter + (t - fromEpochS) / span * plotW
            fun y(c: Double) = ((yMax - c) / (yMax - yMin)).toFloat() * size.height

            ticks.forEach { v ->
                drawLine(CoolerColors.Muted.copy(alpha = 0.25f), Offset(gutter, y(v)), Offset(size.width, y(v)), 1.dp.toPx())
                val label = measurer.measure("%.0f".format(v), tickStyle)
                drawText(
                    label,
                    topLeft = Offset(
                        gutter - label.size.width - 4.dp.toPx(),
                        (y(v) - label.size.height / 2f).coerceIn(0f, size.height - label.size.height),
                    ),
                )
            }

            segments.forEach { seg ->
                seg.zipWithNext().forEach { (a, b) ->
                    if (a.relay == true) {
                        drawRect(CoolerColors.RelayTint, Offset(x(a.epochS), 0f), Size(x(b.epochS) - x(a.epochS), size.height))
                    }
                }
            }
            if (bandLo != null && bandHi != null) {
                drawRect(CoolerColors.Band, Offset(gutter, y(bandHi)), Size(plotW, y(bandLo) - y(bandHi)))
            }
            val line = Path()
            segments.forEach { seg ->
                seg.forEachIndexed { i, s ->
                    if (i == 0) line.moveTo(x(s.epochS), y(s.tempC)) else line.lineTo(x(s.epochS), y(s.tempC))
                }
                // A lone reading between gaps has no line to sit on.
                if (seg.size == 1) drawCircle(CoolerColors.Accent, 2.dp.toPx(), Offset(x(seg[0].epochS), y(seg[0].tempC)))
            }
            drawPath(line, CoolerColors.Accent, style = Stroke(width = 2.dp.toPx()))
        }
        Text("min ${StatusText.temp(temps.min())}°", color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
    }
}
