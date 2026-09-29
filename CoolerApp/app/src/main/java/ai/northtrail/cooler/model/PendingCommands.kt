package ai.northtrail.cooler.model

data class Pending(val value: Int, val sentAtMillis: Long)

data class Resolution(
    val pending: Map<String, Pending>,
    val confirmed: Set<String>,
    val timedOut: Set<String>,
)

object PendingCommands {
    const val TIMEOUT_MS = 3_000L

    /**
     * Confirmed only by a live /data received after sending that reports the
     * requested value: a retained copy, or a periodic publish that crossed the
     * command in flight, still carries the old value.
     */
    fun resolve(
        pending: Map<String, Pending>,
        state: CoolerState?,
        stateLiveAtMillis: Long?,
        nowMillis: Long,
    ): Resolution {
        val confirmed = mutableSetOf<String>()
        val timedOut = mutableSetOf<String>()
        val remaining = mutableMapOf<String, Pending>()
        for ((key, p) in pending) {
            val live = stateLiveAtMillis != null && stateLiveAtMillis >= p.sentAtMillis
            when {
                live && state?.settings?.get(key) == p.value -> confirmed += key
                nowMillis - p.sentAtMillis >= TIMEOUT_MS -> timedOut += key
                else -> remaining[key] = p
            }
        }
        return Resolution(remaining, confirmed, timedOut)
    }
}
