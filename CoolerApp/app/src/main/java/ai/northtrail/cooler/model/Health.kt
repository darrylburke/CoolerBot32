package ai.northtrail.cooler.model

enum class LinkState { DISCONNECTED, CONNECTING, CONNECTED, REJECTED, TLS_FAILED }

data class LinkStatus(
    val state: LinkState = LinkState.DISCONNECTED,
    val detail: String = "",
    val connectedAtMillis: Long? = null,
)

enum class LinkView { CONNECTING, REJECTED, TLS_FAILED, UNREACHABLE, WAITING, CONTROLLER_OFFLINE, SILENT, ONLINE }

/** Shown while the app is open. There is no acknowledge: nothing here notifies. */
enum class Banner(val text: String) {
    CONTROLLER_OFFLINE("Controller offline"),
    CONTROLLER_SILENT("Controller silent: no data for 5 min"),
    AC_NOT_RESPONDING("AC not responding"),
    BOX_SENSOR_FAULT("Box sensor fault"),
    FIN_SENSOR_FAULT("Fin sensor fault"),
}

data class Health(
    val link: LinkView,
    val label: String,
    val banners: List<Banner>,
    /** Since the last live /data; null when only a retained copy has been seen. */
    val dataAgeMillis: Long?,
) {
    val controlsEnabled: Boolean get() = link == LinkView.ONLINE

    fun stripText(): String {
        val age = dataAgeMillis?.let(::formatAge)
        return when {
            link == LinkView.ONLINE -> if (age == null) "Online · waiting for update" else "Online · updated $age ago"
            age != null -> "$label · last data $age ago"
            else -> label
        }
    }
}

object Liveness {
    /** The controller publishes every 30 s; the panel calls it silent after 5 min. */
    const val SILENT_AFTER_MS = 300_000L

    fun evaluate(
        link: LinkStatus,
        online: Boolean?,
        state: CoolerState?,
        lastLiveDataAtMillis: Long?,
        nowMillis: Long,
    ): Health {
        val (view, label) = when (link.state) {
            LinkState.REJECTED -> LinkView.REJECTED to "Broker refused access"
            LinkState.TLS_FAILED -> LinkView.TLS_FAILED to "Can't verify the broker (${link.detail})"
            LinkState.CONNECTING -> LinkView.CONNECTING to "Connecting…"
            LinkState.DISCONNECTED -> LinkView.UNREACHABLE to "Broker unreachable"
            LinkState.CONNECTED -> when {
                online == false -> LinkView.CONTROLLER_OFFLINE to "Controller offline"
                state == null -> LinkView.WAITING to "Waiting for controller"
                else -> {
                    // From the later of the last live /data and connecting, so the
                    // retained copy seen on connect doesn't count as silence.
                    val reference = maxOf(lastLiveDataAtMillis ?: 0L, link.connectedAtMillis ?: 0L)
                    if (nowMillis - reference > SILENT_AFTER_MS) LinkView.SILENT to "No data for 5 min"
                    else LinkView.ONLINE to "Online"
                }
            }
        }
        val banners = buildList {
            if (view == LinkView.CONTROLLER_OFFLINE) add(Banner.CONTROLLER_OFFLINE)
            if (view == LinkView.SILENT) add(Banner.CONTROLLER_SILENT)
            if (state?.noResponse == true) add(Banner.AC_NOT_RESPONDING)
            if (state?.shtFault == true) add(Banner.BOX_SENSOR_FAULT)
            if (state?.finFault == true) add(Banner.FIN_SENSOR_FAULT)
        }
        return Health(view, label, banners, lastLiveDataAtMillis?.let { nowMillis - it })
    }
}

fun formatAge(millis: Long): String {
    val s = millis / 1_000
    return when {
        s < 60 -> "${s}s"
        s < 3_600 -> "${s / 60}m"
        else -> "${s / 3_600}h"
    }
}
