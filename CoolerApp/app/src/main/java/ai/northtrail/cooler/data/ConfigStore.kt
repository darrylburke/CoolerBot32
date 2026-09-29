package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.BrokerConfig
import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Broker settings in private prefs; the password AES-GCM encrypted with an Android Keystore key. */
class ConfigStore(context: Context) {
    private val preferences = context.getSharedPreferences("cooler_config", Context.MODE_PRIVATE)

    fun load(): BrokerConfig = loadWithoutPassword().copy(password = decryptPassword())

    /** Everything but the password (left empty); no Keystore access, safe for the UI thread. */
    fun loadWithoutPassword(): BrokerConfig {
        val defaults = BrokerConfig()
        return BrokerConfig(
            host = preferences.getString("host", defaults.host).orEmpty(),
            port = preferences.getInt("port", defaults.port),
            username = preferences.getString("username", defaults.username).orEmpty(),
            password = "",
            base = preferences.getString("base", defaults.base).orEmpty(),
        )
    }

    fun isConfigured(): Boolean = preferences.getBoolean("configured", false) && load().isUsable

    fun save(config: BrokerConfig) {
        require(config.isUsable) { "Broker configuration is incomplete" }
        val (ciphertext, iv) = encrypt(config.password)
        preferences.edit()
            .putString("host", config.host.trim())
            .putInt("port", config.port)
            .putString("username", config.username.trim())
            .putString("base", config.base.trim().trimEnd('/'))
            .putString("password_ciphertext", ciphertext)
            .putString("password_iv", iv)
            .putBoolean("configured", true)
            .apply()
    }

    /** Stable per-install id, so the broker sees one client per phone. */
    fun uiId(): String {
        val existing = preferences.getString("ui_id", null)
        if (!existing.isNullOrBlank()) return existing
        val created = "android-${UUID.randomUUID().toString().replace("-", "").take(12)}"
        preferences.edit().putString("ui_id", created).apply()
        return created
    }

    private fun encrypt(cleartext: String): Pair<String, String> {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, secretKey())
        return Base64.encodeToString(cipher.doFinal(cleartext.toByteArray()), Base64.NO_WRAP) to
            Base64.encodeToString(cipher.iv, Base64.NO_WRAP)
    }

    private fun decryptPassword(): String {
        val ciphertext = preferences.getString("password_ciphertext", null) ?: return ""
        val iv = preferences.getString("password_iv", null) ?: return ""
        return runCatching {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, secretKey(), GCMParameterSpec(128, Base64.decode(iv, Base64.NO_WRAP)))
            String(cipher.doFinal(Base64.decode(ciphertext, Base64.NO_WRAP)))
        }.getOrDefault("")
    }

    private fun secretKey(): SecretKey {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        generator.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build(),
        )
        return generator.generateKey()
    }

    private companion object {
        const val KEY_ALIAS = "cooler_mqtt_password"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
    }
}
