package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.LinkStatus

/**
 * The decisions around (re)connecting that don't need a Context: whether the
 * app is in the foreground (spec §7: connected only while visible), and which
 * config last failed fatally (spec §8: no retry until the config changes).
 */
class ConnectGate {
    var foreground = false
        private set
    private var fatalConfig: BrokerConfig? = null
    private var fatalStatus: LinkStatus? = null

    fun enterForeground() { foreground = true }
    fun leaveForeground() { foreground = false }

    fun recordFatal(config: BrokerConfig, status: LinkStatus) {
        fatalConfig = config
        fatalStatus = status
    }

    /** The fatal status to show instead of dialing again, if [config] already failed that way. */
    fun fatalFor(config: BrokerConfig): LinkStatus? = if (config == fatalConfig) fatalStatus else null

    fun clearFatal() {
        fatalConfig = null
        fatalStatus = null
    }
}
