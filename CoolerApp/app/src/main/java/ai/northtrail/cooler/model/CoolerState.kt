package ai.northtrail.cooler.model

/** The controller's ten settings, as carried in `/data` and accepted on `/cmd` (spec §4). */
val SETTING_KEYS: List<String> = listOf(
    "coolerset", "range", "sampleinterval",
    "fin_cutoff", "fin_recover", "settle",
    "minofftime", "minruntime", "maxrun", "dutypercent",
)

/**
 * One `cooler/data` snapshot. A null means the controller sent null or left the
 * field out; the UI shows it as "--".
 */
data class CoolerState(
    val temp: Double? = null,
    val humidity: Double? = null,
    val finTemp: Double? = null,
    val finOhms: Double? = null,
    /** °C per minute. */
    val finSlope: Double? = null,
    val mode: String? = null,
    val overrideSrc: String? = null,
    val state: String? = null,
    val relay: Boolean = false,
    val coolCall: Boolean = false,
    val defrost: Boolean = false,
    /** 1 running, 0 stopped, null when the fin sensor can't tell. */
    val compressor: Int? = null,
    val shtFault: Boolean = false,
    val finFault: Boolean = false,
    val noResponse: Boolean = false,
    val runS: Long = 0,
    val offS: Long = 0,
    val holdS: Long = 0,
    val settings: Map<String, Int> = emptyMap(),
    val finCal: Boolean = false,
    val calActive: Boolean = false,
    val calPoints: Int = 0,
    val calSpan: Double = 0.0,
    val histIntervalS: Int? = null,
    /** Epoch seconds of the newest `tempHist` sample (controller clock, SNTP). */
    val histLastTs: Long? = null,
    /** Whole degrees, oldest first. */
    val tempHist: List<Double> = emptyList(),
    val uptimeS: Long? = null,
)
