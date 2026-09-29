package ai.northtrail.cooler

import ai.northtrail.cooler.data.ConfigStore
import ai.northtrail.cooler.data.MqttRepository
import android.app.Application

class CoolerApplication : Application() {
    lateinit var configStore: ConfigStore
        private set
    lateinit var repository: MqttRepository
        private set

    override fun onCreate() {
        super.onCreate()
        configStore = ConfigStore(this)
        repository = MqttRepository(this, configStore)
    }
}
