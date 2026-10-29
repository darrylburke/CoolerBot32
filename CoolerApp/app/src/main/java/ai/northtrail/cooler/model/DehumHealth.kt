package ai.northtrail.cooler.model

/** How the dehumidifier looks from here, judged apart from the Cooler. */
enum class DehumView(val text: String) {
    NO_ACCESS("No dehumidifier access (check the broker ACL)"),
    BROKER_DOWN("Broker unreachable"),
    WAITING("Waiting for dehumidifier"),
    OFFLINE("Dehumidifier offline"),
    SILENT("Dehumidifier silent: no data for 90 s"),
    ONLINE("Online"),
}

object DehumHealth {
    /** It publishes every 30 s: three missed messages is silence. */
    const val SILENT_AFTER_MS = 90_000L

    fun evaluate(
        link: LinkStatus,
        online: Boolean?,
        state: DehumState?,
        lastLiveDataAtMillis: Long?,
        nowMillis: Long,
    ): DehumView = when {
        DehumTopics.DATA in link.refused -> DehumView.NO_ACCESS
        link.state != LinkState.CONNECTED -> DehumView.BROKER_DOWN
        online == false -> DehumView.OFFLINE
        state == null -> DehumView.WAITING
        // From the later of the last live data and connecting, so the retained copy seen on connect isn't silence.
        nowMillis - maxOf(lastLiveDataAtMillis ?: 0L, link.connectedAtMillis ?: 0L) > SILENT_AFTER_MS -> DehumView.SILENT
        else -> DehumView.ONLINE
    }
}
