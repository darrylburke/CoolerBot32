package ai.northtrail.cooler

import ai.northtrail.cooler.data.CoolerTransport
import ai.northtrail.cooler.data.IncomingMessage
import ai.northtrail.cooler.model.DehumBounds
import ai.northtrail.cooler.model.DehumCommands
import ai.northtrail.cooler.model.DehumHealth
import ai.northtrail.cooler.model.DehumHistoryParser
import ai.northtrail.cooler.model.DehumMode
import ai.northtrail.cooler.model.DehumState
import ai.northtrail.cooler.model.DehumStateParser
import ai.northtrail.cooler.model.DehumTopics
import ai.northtrail.cooler.model.DehumView
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.Pending
import ai.northtrail.cooler.model.PendingCommands
import ai.northtrail.cooler.model.Publish
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

data class DehumUi(
    val view: DehumView = DehumView.BROKER_DOWN,
    val link: LinkStatus = LinkStatus(),
    /** From `dehum/availability`; null until seen. */
    val online: Boolean? = null,
    val state: DehumState? = null,
    /** When [state] arrived, if it came from a live (non-retained) message. */
    val stateLiveAtMillis: Long? = null,
    val lastLiveDataAtMillis: Long? = null,
    /** Humidity over time ([ai.northtrail.cooler.model.Sample.value] is %RH). */
    val trend: TrendBuffer = TrendBuffer(),
    val drafts: Map<String, Int> = emptyMap(),
    val pending: Map<String, Pending> = emptyMap(),
    val message: String? = null,
) {
    val controlsEnabled: Boolean get() = view == DehumView.ONLINE

    fun settingValue(key: String): Int? = drafts[key] ?: pending[key]?.value ?: state?.settings?.get(key)

    fun isPending(key: String): Boolean = key in drafts || key in pending
}

/**
 * The dehumidifier side of the app (spec 2026-10-28-app-humidity-tab-design.md).
 * Shares the Cooler's transport; its topics are fixed `dehum/...`.
 */
class DehumController(
    private val transport: CoolerTransport,
    private val scope: CoroutineScope,
    private val clock: () -> Long,
    private val onIgnored: (String) -> Unit = {},
) {
    private val commands = DehumCommands()
    private val state = MutableStateFlow(DehumUi())
    val ui: StateFlow<DehumUi> = state.asStateFlow()
    private val draftJobs = mutableMapOf<String, Job>()

    fun start() {
        scope.launch { transport.messages.collect { onMessage(it) } }
        scope.launch {
            transport.link.collect { l ->
                update { cur ->
                    val fresh = l.state == LinkState.CONNECTED && l.connectedAtMillis != cur.link.connectedAtMillis
                    if (fresh) cur.copy(link = l, lastLiveDataAtMillis = null, stateLiveAtMillis = null) else cur.copy(link = l)
                }
            }
        }
        scope.launch {
            while (isActive) {
                delay(1_000)
                update { it }
            }
        }
    }

    /** Mode buttons send at once: no debounce and no pending (the highlight follows `dehum/data`). */
    fun setMode(m: DehumMode) = act(commands.mode(m))

    fun stepSetting(key: String, direction: Int) {
        val s = state.value
        val bound = DehumBounds.find(key) ?: return
        val current = s.settingValue(key) ?: return
        if (!s.controlsEnabled) return
        val next = (current + direction * bound.step).coerceIn(floor(s, key), bound.hi)
        if (next != current) edit(key, next)
    }

    /** A typed value: sent as is, or refused (false) outside the live range. */
    fun enterSetting(key: String, value: Int): Boolean {
        val s = state.value
        val bound = DehumBounds.find(key) ?: return false
        if (!s.controlsEnabled || value !in floor(s, key)..bound.hi) return false
        edit(key, value)
        return true
    }

    fun consumeMessage() = state.update { it.copy(message = null) }

    private fun floor(s: DehumUi, key: String) = DehumBounds.floor(key, s.settingValue("min_run") ?: 0)

    private fun onMessage(m: IncomingMessage) {
        val now = clock()
        when (m.topic) {
            DehumTopics.AVAILABILITY -> update {
                when (m.payload.trim()) {
                    "online" -> it.copy(online = true)
                    "offline" -> it.copy(online = false)
                    else -> it
                }
            }
            DehumTopics.DATA -> {
                val parsed = DehumStateParser.parse(m.payload)
                if (parsed == null) {
                    onIgnored("ignored ${m.topic}: not dehum/data v${DehumStateParser.VERSION}")
                    return
                }
                update { cur ->
                    val h = parsed.humidity
                    val trend = if (!m.retained && h != null) cur.trend.appended(now / 1_000, h, parsed.relay) else cur.trend
                    cur.copy(
                        state = parsed,
                        stateLiveAtMillis = if (m.retained) null else now,
                        lastLiveDataAtMillis = if (m.retained) cur.lastLiveDataAtMillis else now,
                        trend = trend,
                    )
                }
            }
            DehumTopics.HISTORY -> {
                val w = DehumHistoryParser.parse(m.payload, now / 1_000)
                if (w == null) {
                    onIgnored("ignored ${m.topic}: not a dehum/history v1 window")
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
            if (v == s.state?.settings?.get(key) && key !in s.pending) {
                update { it.copy(drafts = it.drafts - key) }
                return@launch
            }
            val sent = state.value.controlsEnabled && transport.publish(commands.setting(key, v))
            update {
                val cleared = it.copy(drafts = it.drafts - key)
                if (sent) cleared.copy(pending = cleared.pending + (key to Pending(v, clock())))
                else cleared.copy(message = NOT_SENT)
            }
        }
    }

    private fun act(publish: Publish) {
        if (!state.value.controlsEnabled || !transport.publish(publish)) update { it.copy(message = NOT_SENT) }
    }

    /** Applies [change], then recomputes the view and resolves pending commands. */
    private fun update(change: (DehumUi) -> DehumUi) {
        state.update { current ->
            val next = change(current)
            val now = clock()
            val r = PendingCommands.resolve(next.pending, next.state?.settings, next.stateLiveAtMillis, now)
            next.copy(
                view = DehumHealth.evaluate(next.link, next.online, next.state, next.lastLiveDataAtMillis, now),
                pending = r.pending,
                message = if (r.timedOut.isEmpty()) next.message
                else "Not applied: " + r.timedOut.joinToString { key -> DehumBounds.find(key)?.label ?: key },
            )
        }
    }

    companion object {
        const val DEBOUNCE_MS = 400L
        private const val NOT_SENT = "Couldn't send: dehumidifier not reachable"
    }
}
