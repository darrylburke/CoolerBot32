package ai.northtrail.cooler.model

/** A value typed into a setting's field: a whole number inside the row's live range, or nothing. */
object TypedEntry {
    fun parse(text: String, lo: Int, hi: Int): Int? {
        val t = text.trim()
        if (t.startsWith('+')) return null
        val v = t.toIntOrNull() ?: return null
        return v.takeIf { it in lo..hi }
    }

    fun hint(lo: Int, hi: Int): String = "$lo–$hi"
}
