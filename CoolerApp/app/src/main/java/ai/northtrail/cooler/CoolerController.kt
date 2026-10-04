package ai.northtrail.cooler

import ai.northtrail.cooler.data.CoolerTransport
import ai.northtrail.cooler.data.IncomingMessage
import ai.northtrail.cooler.model.Bounds
import ai.northtrail.cooler.model.Commands
import ai.northtrail.cooler.model.CoolerState
import ai.northtrail.cooler.model.CoolerStateParser
import ai.northtrail.cooler.model.Health
import ai.northtrail.cooler.model.HistoryParser
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.Liveness
import ai.northtrail.cooler.model.Pending
import ai.northtrail.cooler.model.PendingCommands
import ai.northtrail.cooler.model.Publish
import ai.northtrail.cooler.model.Topics
import ai.northtrail.cooler.model.TrendBuffer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

data class UiState(
    val configured: Boolean,
    val link: LinkStatus = LinkStatus(),
    /** From `<base>/availability`; null until seen. */
    val online: Boolean? = null,
    val state: CoolerState? = null,
    /** When [state] arrived, if it came from a live (non-retained) message. */
    val stateLiveAtMillis: Long? = null,
    val lastLiveDataAtMillis: Long? = null,
    val trend: TrendBuffer = TrendBuffer(),
    val health: Health = Liveness.evaluate(LinkStatus(), null, null, null, 0L),
    /** Edits still inside the debounce. */
    val drafts: Map<String, Int> = emptyMap(),
    /** Sent, waiting for the controller to report them. */
    val pending: Map<String, Pending> = emptyMap(),
    val message: String? = null,
) {
    /** What a setting row shows: the edit in progress, else the value sent, else the controller's. */
    fun settingValue(key: String): Int? = drafts[key] ?: pending[key]?.value ?: state?.settings?.get(key)

    fun isPending(key: String): Boolean = key in drafts || key in pending
}

class CoolerController(
    private val transport: CoolerTransport,
    topics: Topics,
    private val scope: CoroutineScope,
    private val clock: () -> Long,
    configured: Boolean,
    private val onIgnored: (String) -> Unit = {},
) {
    @Volatile private var topics = topics
    @Volatile private var commands = Commands(topics)
    private val state = MutableStateFlow(UiState(configured = configured))
    val ui: StateFlow<UiState> = state.asStateFlow()
    private val draftJobs = mutableMapOf<String, Job>()

    fun start() {
        scope.launch { transport.messages.collect { onMessage(it) } }
        scope.launch {
            transport.link.collect { l ->
                update { cur ->
                    // A new session: the last session's data age says nothing about this one.
                    val fresh = l.state == LinkState.CONNECTED && l.connectedAtMillis != cur.link.connectedAtMillis
                    if (fresh) cur.copy(link = l, lastLiveDataAtMillis = null, stateLiveAtMillis = null)
                    else cur.copy(link = l)
                }
            }
        }
        // Re-evaluate silence and command timeouts even when nothing arrives.
        scope.launch {
            while (isActive) {
                delay(1_000)
                update { it }
            }
        }
    }

    /** A new topic base is a different cooler: forget everything seen on the old one. */
    fun reset(newTopics: Topics) {
        topics = newTopics
        commands = Commands(newTopics)
        draftJobs.values.forEach { it.cancel() }
        draftJobs.clear()
        update { UiState(configured = it.configured, link = it.link) }
    }

    fun stepSetting(key: String, direction: Int) {
        val s = state.value
        val bound = Bounds.find(key) ?: return
        val current = s.settingValue(key) ?: return
        if (!s.health.controlsEnabled) return
        val next = Bounds.clamp(key, current + direction * bound.step, s.settingValue("fin_cutoff") ?: 0)
        if (next != current) edit(key, next)
    }

    fun chooseSetting(key: String, value: Int) {
        val s = state.value
        if (!s.health.controlsEnabled || Bounds.find(key) == null) return
        val next = Bounds.clamp(key, value, s.settingValue("fin_cutoff") ?: 0)
        if (next != s.settingValue(key)) edit(key, next)
    }

    fun startCalibration() = act(commands.calibrate(start = true))
    fun abortCalibration() = act(commands.calibrate(start = false))
    fun resetFinCal() = act(commands.resetFinCal())

    fun consumeMessage() = state.update { it.copy(message = null) }
    fun markConfigured() = update { it.copy(configured = true) }

    private fun onMessage(m: IncomingMessage) {
        val t = topics
        val now = clock()
        when (m.topic) {
            t.availability -> update {
                when (m.payload.trim()) {
                    "online" -> it.copy(online = true)
                    "offline" -> it.copy(online = false)
                    else -> it
                }
            }
            t.data -> {
                val parsed = CoolerStateParser.parse(m.payload)
                if (parsed == null) {
                    onIgnored("ignored ${m.topic}: not cooler/data v${CoolerStateParser.VERSION}")
                    return
                }
                update { cur ->
                    var trend = cur.trend
                    if (m.retained || trend.all.isEmpty()) trend = trend.seeded(parsed)
                    if (!m.retained) trend = trend.appended(now / 1_000, parsed.temp, parsed.relay, parsed.humidity)
                    cur.copy(
                        state = parsed,
                        stateLiveAtMillis = if (m.retained) null else now,
                        lastLiveDataAtMillis = if (m.retained) cur.lastLiveDataAtMillis else now,
                        trend = trend,
                    )
                }
            }
            t.history -> {
                val w = HistoryParser.parse(m.payload, now / 1_000)
                if (w == null) {
                    onIgnored("ignored ${m.topic}: not cooler/history v${HistoryParser.VERSION}")
                    return
                }
                update { it.copy(trend = it.trend.withHistory(w)) }
            }
        }
    }

    private fun edit(key: String, value: Int) {
        update { it.copy(drafts = it.drafts + (key to value)) }
        draftJobs[key]?.cancel()
        draftJobs[key] = scope.launch {
            delay(DEBOUNCE_MS)
            val s = state.value
            val v = s.drafts[key] ?: return@launch
            // A burst that ends where it started asks the controller for nothing.
            if (v == s.state?.settings?.get(key) && key !in s.pending) {
                update { it.copy(drafts = it.drafts - key) }
                return@launch
            }
            send(key, v)
        }
    }

    private fun send(key: String, value: Int) {
        val sent = state.value.health.controlsEnabled && transport.publish(commands.setting(key, value))
        update {
            val cleared = it.copy(drafts = it.drafts - key)
            if (sent) cleared.copy(pending = cleared.pending + (key to Pending(value, clock())))
            else cleared.copy(message = NOT_SENT)
        }
    }

    private fun act(publish: Publish) {
        if (!state.value.health.controlsEnabled || !transport.publish(publish)) {
            update { it.copy(message = NOT_SENT) }
        }
    }

    /** Applies [change], then recomputes health and resolves pending commands. */
    private fun update(change: (UiState) -> UiState) {
        state.update { current ->
            val next = change(current)
            val now = clock()
            val r = PendingCommands.resolve(next.pending, next.state, next.stateLiveAtMillis, now)
            next.copy(
                health = Liveness.evaluate(next.link, next.online, next.state, next.lastLiveDataAtMillis, now),
                pending = r.pending,
                message = if (r.timedOut.isEmpty()) next.message
                else "Not applied: " + r.timedOut.joinToString { key -> Bounds.find(key)?.label ?: key },
            )
        }
    }

    companion object {
        const val DEBOUNCE_MS = 400L
        private const val NOT_SENT = "Couldn't send: controller not reachable"
    }
}
