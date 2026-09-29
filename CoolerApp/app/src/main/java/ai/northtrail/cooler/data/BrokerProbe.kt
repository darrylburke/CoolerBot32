package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.Topics
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.atomic.AtomicBoolean
import javax.net.ssl.TrustManagerFactory

sealed interface ProbeResult {
    data object Ok : ProbeResult
    data object LoginRefused : ProbeResult
    data class TlsFailed(val reason: String) : ProbeResult
    data object Unreachable : ProbeResult
    data class NoData(val topic: String) : ProbeResult
}

/** What Setup shows; null means the broker answered with cooler data. */
fun ProbeResult.message(): String? = when (this) {
    ProbeResult.Ok -> null
    ProbeResult.LoginRefused -> "Login refused: check the username, password and broker ACL"
    is ProbeResult.TlsFailed -> "Can't verify the broker ($reason)"
    ProbeResult.Unreachable -> "Can't reach the broker"
    is ProbeResult.NoData -> "Connected, but no cooler data on $topic"
}

/**
 * Setup's "Test & save": connect with the entered values and wait for the
 * retained `<base>/data`, so a wrong password, ACL or topic base is caught
 * before anything is saved.
 */
object BrokerProbe {
    const val TIMEOUT_MS = 10_000L

    suspend fun run(
        config: BrokerConfig,
        trust: TrustManagerFactory?,
        clientId: String,
        timeoutMs: Long = TIMEOUT_MS,
    ): ProbeResult {
        val topics = Topics(config.base)
        val result = CompletableDeferred<ProbeResult>()
        val subscribed = AtomicBoolean(false)
        val session = HiveMqSession(
            host = config.host.trim(),
            port = config.port,
            username = config.username.trim(),
            password = config.password.toByteArray(),
            clientId = clientId,
            trustManagerFactory = trust,
            subscriptions = topics.subscriptions,
            onLink = { state, detail, _ ->
                when (state) {
                    LinkState.CONNECTED -> subscribed.set(true)
                    LinkState.REJECTED -> result.complete(ProbeResult.LoginRefused)
                    LinkState.TLS_FAILED -> result.complete(ProbeResult.TlsFailed(detail))
                    LinkState.DISCONNECTED -> result.complete(ProbeResult.Unreachable)
                    LinkState.CONNECTING -> Unit
                }
            },
            onMessage = { if (it.topic == topics.data) result.complete(ProbeResult.Ok) },
        )
        session.start()
        return try {
            withTimeoutOrNull(timeoutMs) { result.await() }
                ?: if (subscribed.get()) ProbeResult.NoData(topics.data) else ProbeResult.Unreachable
        } finally {
            session.stop()
        }
    }
}
