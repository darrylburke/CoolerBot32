package ai.northtrail.cooler.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

object CoolerColors {
    val Background = Color(0xFF0F1720)
    val Surface = Color(0xFF1A2532)
    val Text = Color(0xFFE6EDF3)
    val Muted = Color(0xFF8B98A5)
    val Accent = Color(0xFF78C8FF)
    val Warn = Color(0xFFFFB74D)
    val WarnBackground = Color(0xFF3A2A10)
    val Ok = Color(0xFF7EE787)
    val OkBackground = Color(0xFF12301F)
    val Bad = Color(0xFFFF8A80)
    val BadBackground = Color(0xFF3A1616)
    val Band = Color(0x3378C8FF)
    val RelayTint = Color(0x2278C8FF)
    /** Humidity trace and its axis: the panel's olive, lifted for this darker background. */
    val Humidity = Color(0xFFA9BC8A)
}

@Composable
fun CoolerTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = CoolerColors.Accent,
            onPrimary = CoolerColors.Background,
            background = CoolerColors.Background,
            surface = CoolerColors.Background,
            surfaceVariant = CoolerColors.Surface,
            onBackground = CoolerColors.Text,
            onSurface = CoolerColors.Text,
        ),
        content = content,
    )
}
