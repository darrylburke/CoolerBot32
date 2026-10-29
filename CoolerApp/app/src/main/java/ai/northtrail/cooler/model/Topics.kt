package ai.northtrail.cooler.model

/** The cooler's topics under a base (default `cooler`). */
class Topics(base: String = "cooler") {
    private val b = base.trim().trimEnd('/')

    val data = "$b/data"
    val availability = "$b/availability"
    val cmd = "$b/cmd"
    val history = "$b/history"

    /** Exact topics only: the broker login may read nothing else. A refusal of these is fatal. */
    val subscriptions: List<String> = listOf(data, availability)

    /**
     * Wanted but not needed: a broker ACL without `history` or the dehumidifier's topics
     * refuses them, and the app works on without.
     */
    val optionalSubscriptions: List<String> = listOf(history) + DehumTopics.subscriptions
}
