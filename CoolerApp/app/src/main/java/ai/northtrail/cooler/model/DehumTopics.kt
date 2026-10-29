package ai.northtrail.cooler.model

/** The dehumidifier's topics: fixed by its firmware and the Node-RED flow, not by the app's topic base. */
object DehumTopics {
    const val DATA = "dehum/data"
    const val AVAILABILITY = "dehum/availability"
    const val CMD = "dehum/cmd"
    const val HISTORY = "dehum/history"
    val subscriptions: List<String> = listOf(DATA, AVAILABILITY, HISTORY)
}
