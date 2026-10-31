package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Test

class SeriesShownTest {
    @Test
    fun allShowByDefaultAndEachToggles() {
        val s = SeriesShown()
        assertEquals(SeriesShown(temp = true, rh = true, avg = true, rhAvg = true), s)
        assertEquals(SeriesShown(rhAvg = false), s.toggled(Series.RH_AVG))
        assertEquals(SeriesShown(temp = true, rh = false, avg = true), s.toggled(Series.RH))
        assertEquals(s, s.toggled(Series.RH).toggled(Series.RH))
    }

    @Test
    fun anyOrAllCanBeHidden() {
        val none = SeriesShown().toggled(Series.TEMP).toggled(Series.RH).toggled(Series.AVG).toggled(Series.RH_AVG)
        assertEquals(SeriesShown(temp = false, rh = false, avg = false, rhAvg = false), none)
        assertEquals(SeriesShown(temp = true, rh = false, avg = false, rhAvg = false), none.toggled(Series.TEMP))
    }
}
