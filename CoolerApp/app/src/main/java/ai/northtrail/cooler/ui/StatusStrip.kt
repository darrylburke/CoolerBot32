package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.Banner
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.LinkView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun StatusStrip(health: Health, modifier: Modifier = Modifier) {
    val (fg, bg) = when (health.link) {
        LinkView.ONLINE -> CoolerColors.Ok to CoolerColors.OkBackground
        LinkView.CONNECTING, LinkView.WAITING -> CoolerColors.Muted to CoolerColors.Surface
        else -> CoolerColors.Bad to CoolerColors.BadBackground
    }
    Text(
        "● " + health.stripText(),
        color = fg,
        style = MaterialTheme.typography.labelLarge,
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .background(bg, RoundedCornerShape(12.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp),
    )
}

@Composable
fun Banners(banners: List<Banner>) {
    banners.forEach { b ->
        Text(
            b.text,
            color = CoolerColors.Bad,
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 4.dp)
                .background(CoolerColors.BadBackground, RoundedCornerShape(12.dp))
                .padding(12.dp),
        )
    }
}
