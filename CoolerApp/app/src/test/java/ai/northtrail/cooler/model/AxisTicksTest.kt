package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AxisTicksTest {
    @Test
    fun aWideRangeGetsFiveDegreeSteps() {
        assertEquals(listOf(0.0, 5.0, 10.0), AxisTicks.of(-1.9, 14.0))
    }

    @Test
    fun aNarrowRangeGetsWholeDegrees() {
        assertEquals(listOf(3.0, 4.0, 5.0), AxisTicks.of(2.5, 5.5))
    }

    @Test
    fun sixWholeDegreesIsTooManySoTwoDegreeSteps() {
        assertEquals(listOf(2.0, 4.0, 6.0), AxisTicks.of(2.0, 7.0))
    }

    @Test
    fun belowZeroWorksToo() {
        assertEquals(listOf(-12.0, -10.0, -8.0, -6.0, -4.0), AxisTicks.of(-12.0, -3.0))
    }

    @Test
    fun neverMoreThanFive() {
        for (hi in listOf(1.0, 3.3, 8.0, 17.0, 33.0, 90.0, 400.0)) {
            val t = AxisTicks.of(-1.0, hi)
            assertTrue("$hi -> $t", t.size in 1..AxisTicks.MAX_TICKS)
        }
    }

    @Test
    fun anEmptyRangeHasNoTicks() {
        assertEquals(emptyList<Double>(), AxisTicks.of(5.0, 5.0))
        assertEquals(emptyList<Double>(), AxisTicks.of(6.0, 5.0))
    }
}
