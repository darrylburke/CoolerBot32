package ai.northtrail.cooler.model

import kotlin.math.ceil
import kotlin.math.floor

/** Temperature axis labels: whole-degree "nice" steps (1, 2, 5, 10, ...) inside the visible range. */
object AxisTicks {
    const val MAX_TICKS = 5
    private val STEPS = listOf(1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0)

    fun of(yMin: Double, yMax: Double): List<Double> {
        if (yMax <= yMin) return emptyList()
        for (step in STEPS) {
            val first = ceil(yMin / step).toInt()
            val last = floor(yMax / step).toInt()
            if (last - first + 1 <= MAX_TICKS) return (first..last).map { it * step }
        }
        return emptyList()
    }
}
