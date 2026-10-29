package ai.northtrail.cooler.ui

import ai.northtrail.cooler.DehumUi
import ai.northtrail.cooler.model.DehumMode
import ai.northtrail.cooler.model.DehumView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/** The dehumidifier's tab: a Graph page and a Controls page (spec 2026-10-28-app-humidity-tab-design.md). */
@Composable
fun HumidityScreen(
    ui: DehumUi,
    nowEpochS: Long,
    onMode: (DehumMode) -> Unit,
    onStep: (String, Int) -> Unit,
    onEnter: (String, Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    var page by rememberSaveable { mutableIntStateOf(0) }
    Column(modifier.fillMaxSize()) {
        PrimaryTabRow(selectedTabIndex = page, containerColor = CoolerColors.Background) {
            listOf("Graph", "Controls").forEachIndexed { i, title ->
                Tab(selected = page == i, onClick = { page = i }, text = { Text(title) })
            }
        }
        DehumStrip(ui.view)
        if (page == 0) HumidityGraph(ui, nowEpochS) else HumidityControls(ui, onMode, onStep, onEnter)
    }
}

@Composable
private fun DehumStrip(view: DehumView) {
    val (fg, bg) = when (view) {
        DehumView.ONLINE -> CoolerColors.Ok to CoolerColors.OkBackground
        DehumView.WAITING -> CoolerColors.Muted to CoolerColors.Surface
        else -> CoolerColors.Bad to CoolerColors.BadBackground
    }
    Text(
        "● " + view.text,
        color = fg,
        style = MaterialTheme.typography.labelLarge,
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .background(bg, RoundedCornerShape(12.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp),
    )
}
