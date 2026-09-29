package ai.northtrail.cooler.model

/** The cooler's three topics under a base (default `cooler`). */
class Topics(base: String = "cooler") {
    private val b = base.trim().trimEnd('/')

    val data = "$b/data"
    val availability = "$b/availability"
    val cmd = "$b/cmd"

    /** Exact topics only: the broker login may read nothing else. */
    val subscriptions: List<String> = listOf(data, availability)
}
