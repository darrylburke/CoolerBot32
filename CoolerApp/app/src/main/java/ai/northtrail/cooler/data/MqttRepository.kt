package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.LinkState
import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.Publish
import ai.northtrail.cooler.model.Topics
import android.content.Context
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import java.security.KeyStore
import java.security.cert.CertificateFactory
import javax.net.ssl.TrustManagerFactory

class MqttRepository(
    private val context: Context,
    private val configStore: ConfigStore,
) : CoolerTransport {
    private val mutableLink = MutableStateFlow(LinkStatus())
    override val link: StateFlow<LinkStatus> = mutableLink.asStateFlow()
    private val mutableMessages = MutableSharedFlow<IncomingMessage>(
        extraBufferCapacity = 256,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    override val messages: SharedFlow<IncomingMessage> = mutableMessages.asSharedFlow()

    @Volatile private var session: HiveMqSession? = null

    private val gate = ConnectGate()

    /** Saves [config] and reconnects with it, but only while the app is visible (spec §7). */
    @Synchronized
    fun saveAndConnect(config: BrokerConfig) {
        configStore.save(config)
        gate.clearFatal()
        halt()
        if (gate.foreground) dial()
    }

    /** Called when the activity becomes visible. */
    @Synchronized
    override fun connect() {
        gate.enterForeground()
        dial()
    }

    /** Called when the activity is no longer visible. */
    @Synchronized
    override fun disconnect() {
        gate.leaveForeground()
        halt()
    }

    private fun dial() {
        val config = configStore.load()
        if (!config.isUsable) return
        if (session?.clientState?.isConnectedOrReconnect == true) return
        // Never drop a session without stopping it: an abandoned client keeps
        // reconnecting with the same client id and knocks the live one off.
        session?.stop()
        session = null
        // Refused or untrusted with this very config: only a config change retries (spec §8).
        gate.fatalFor(config)?.let {
            mutableLink.value = it
            return
        }
        val trust = try {
            createTrustManagerFactory()
        } catch (error: Throwable) {
            mutableLink.value = LinkStatus(LinkState.TLS_FAILED, "TLS setup failed: ${safeMessage(error)}")
            return
        }
        lateinit var created: HiveMqSession
        created = HiveMqSession(
            host = config.host,
            port = config.port,
            username = config.username,
            password = config.password.toByteArray(),
            clientId = "cooler-${configStore.uiId()}",
            trustManagerFactory = trust,
            subscriptions = Topics(config.base).subscriptions,
            optionalSubscriptions = Topics(config.base).optionalSubscriptions,
            // A superseded session must never overwrite the current link state.
            onLink = { state, detail, at -> setLinkFrom(created, config, state, detail, at) },
            onMessage = { if (session === created) mutableMessages.tryEmit(it) },
        )
        session = created
        created.start()
    }

    private fun halt() {
        session?.stop()
        session = null
        mutableLink.value = LinkStatus(LinkState.DISCONNECTED, "Disconnected")
    }

    override fun publish(publish: Publish): Boolean = session?.publish(publish) ?: false

    /** Setup's "Test & save", with the same trust as the real connection. */
    suspend fun probe(config: BrokerConfig): ProbeResult {
        val trust = try {
            createTrustManagerFactory()
        } catch (error: Throwable) {
            return ProbeResult.TlsFailed(safeMessage(error))
        }
        return BrokerProbe.run(config, trust, clientId = "cooler-probe-${configStore.uiId()}")
    }

    @Synchronized
    private fun setLinkFrom(
        source: HiveMqSession,
        config: BrokerConfig,
        state: LinkState,
        detail: String,
        connectedAt: Long?,
    ) {
        if (session !== source) return
        val status = LinkStatus(state, detail, connectedAt)
        if (state == LinkState.REJECTED || state == LinkState.TLS_FAILED) gate.recordFatal(config, status)
        mutableLink.value = status
    }

    /**
     * Trusts only `assets/broker_ca.pem` when it is bundled (the broker's private
     * CA), otherwise the system trust store. TLS is used either way.
     */
    private fun createTrustManagerFactory(): TrustManagerFactory {
        val keyStore = if (BROKER_CA_ASSET in context.assets.list("").orEmpty()) {
            val certificate = context.assets.open(BROKER_CA_ASSET).use { stream ->
                CertificateFactory.getInstance("X.509").generateCertificate(stream)
            }
            KeyStore.getInstance(KeyStore.getDefaultType()).apply {
                load(null)
                setCertificateEntry("broker-ca", certificate)
            }
        } else {
            null
        }
        return TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm()).apply {
            init(keyStore)
        }
    }

    private companion object {
        const val BROKER_CA_ASSET = "broker_ca.pem"
    }
}
