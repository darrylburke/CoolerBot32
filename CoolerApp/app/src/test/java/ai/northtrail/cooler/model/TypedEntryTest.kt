package ai.northtrail.cooler.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TypedEntryTest {
    @Test
    fun parsesWholeNumbersInRange() {
        assertEquals(45, TypedEntry.parse(" 45 ", 1, 240))
        assertEquals(45, TypedEntry.parse("045", 1, 240))
        assertEquals(-3, TypedEntry.parse("-3", -5, 5))
        assertEquals(1, TypedEntry.parse("1", 1, 240))
        assertEquals(240, TypedEntry.parse("240", 1, 240))
    }

    @Test
    fun rejectsEverythingElse() {
        listOf("", " ", "-", "+5", "4.5", "4,5", "abc", "0", "241", "99999999999", "-99999999999")
            .forEach { assertNull(it, TypedEntry.parse(it, 1, 240)) }
    }

    @Test
    fun hintShowsRange() {
        assertEquals("1–240", TypedEntry.hint(1, 240))
        assertEquals("-5–5", TypedEntry.hint(-5, 5))
    }
}
