package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.Sample
import ai.northtrail.cooler.model.StatusText
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
import androidx.compose.ui.unit.dp

/**
 * Box temperature from [fromEpochS] to [toEpochS]: the setpoint band shaded,
 * spans with the relay closed tinted, the temperature as a line.
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
            fun x(t: Long) = (t - fromEpochS) / span * size.width
            fun y(c: Double) = ((yMax - c) / (yMax - yMin)).toFloat() * size.height

            samples.zipWithNext().forEach { (a, b) ->
                if (a.relay == true) {
                    drawRect(CoolerColors.RelayTint, Offset(x(a.epochS), 0f), Size(x(b.epochS) - x(a.epochS), size.height))
                }
            }
            if (bandLo != null && bandHi != null) {
                drawRect(CoolerColors.Band, Offset(0f, y(bandHi)), Size(size.width, y(bandLo) - y(bandHi)))
            }
            val line = Path()
            samples.forEachIndexed { i, s ->
                if (i == 0) line.moveTo(x(s.epochS), y(s.tempC)) else line.lineTo(x(s.epochS), y(s.tempC))
            }
            drawPath(line, CoolerColors.Accent, style = Stroke(width = 2.dp.toPx()))
        }
        Text("min ${StatusText.temp(temps.min())}°", color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
    }
}
