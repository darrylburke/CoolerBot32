package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class SeriesShownTest {
    @Test
    fun bothShowByDefaultAndEachToggles() {
        val s = SeriesShown()
        assertEquals(SeriesShown(temp = true, rh = true), s)
        assertEquals(SeriesShown(temp = true, rh = false), s.toggled(Series.RH))
        assertEquals(SeriesShown(temp = true, rh = true), s.toggled(Series.RH).toggled(Series.RH))
    }

    @Test
    fun theLastVisibleTraceCannotBeHidden() {
        val tempOnly = SeriesShown(temp = true, rh = false)
        assertEquals(tempOnly, tempOnly.toggled(Series.TEMP))
        val rhOnly = SeriesShown(temp = false, rh = true)
        assertEquals(rhOnly, rhOnly.toggled(Series.RH))
        assertEquals(SeriesShown(), rhOnly.toggled(Series.TEMP))
    }

    @Test
    fun theAverageLineTogglesFreelyAndDoesNotCountAsATrace() {
        val s = SeriesShown()
        assertEquals(true, s.avg)
        assertEquals(false, s.toggled(Series.AVG).avg)
        val tempOnly = SeriesShown(temp = true, rh = false, avg = false)
        assertEquals(tempOnly, tempOnly.toggled(Series.TEMP))   // still refused: avg is no trace
    }
}
