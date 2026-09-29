package ai.northtrail.cooler

import ai.northtrail.cooler.data.message
import ai.northtrail.cooler.model.BrokerConfig
import ai.northtrail.cooler.model.Topics
import ai.northtrail.cooler.ui.SetupUi
import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class CoolerViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application as CoolerApplication

    private val controller = CoolerController(
        transport = app.repository,
        topics = Topics(app.configStore.loadWithoutPassword().base),
        scope = viewModelScope,
        clock = System::currentTimeMillis,
        configured = app.configStore.isConfigured(),
        onIgnored = { Log.w(TAG, it) },
    ).also { it.start() }

    val ui: StateFlow<UiState> = controller.ui

    private val setupState = MutableStateFlow(SetupUi())
    val setup: StateFlow<SetupUi> = setupState.asStateFlow()

    private val brokerConfigState = MutableStateFlow(app.configStore.loadWithoutPassword())

    /** The saved broker settings, minus the password (it is never shown again). Never decrypts. */
    val brokerConfig: StateFlow<BrokerConfig> = brokerConfigState.asStateFlow()

    fun connect() = app.repository.connect()
    fun disconnect() = app.repository.disconnect()

    /** Probe first; save and connect only if the broker answered with cooler data. */
    fun testAndSave(config: BrokerConfig, onSaved: () -> Unit) {
        if (setupState.value.probing) return
        setupState.value = SetupUi(probing = true)
        viewModelScope.launch {
            val error = app.repository.probe(config).message()
            if (error == null) {
                val newTopics = Topics(config.base)
                if (newTopics.data != Topics(brokerConfigState.value.base).data) controller.reset(newTopics)
                app.repository.saveAndConnect(config)
                brokerConfigState.value = app.configStore.loadWithoutPassword()
                controller.markConfigured()
                onSaved()
            }
            setupState.value = SetupUi(probing = false, error = error)
        }
    }

    fun stepSetting(key: String, direction: Int) = controller.stepSetting(key, direction)
    fun chooseSetting(key: String, value: Int) = controller.chooseSetting(key, value)
    fun startCalibration() = controller.startCalibration()
    fun abortCalibration() = controller.abortCalibration()
    fun resetFinCal() = controller.resetFinCal()
    fun consumeMessage() = controller.consumeMessage()

    private companion object {
        const val TAG = "Cooler"
    }
}
