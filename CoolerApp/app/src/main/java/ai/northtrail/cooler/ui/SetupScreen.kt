package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.BrokerConfig
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp

data class SetupUi(val probing: Boolean = false, val error: String? = null)

@Composable
fun SetupScreen(
    initial: BrokerConfig,
    setup: SetupUi,
    onTestAndSave: (BrokerConfig) -> Unit,
    onCancel: (() -> Unit)?,
) {
    var host by rememberSaveable { mutableStateOf(initial.host) }
    var port by rememberSaveable { mutableStateOf(initial.port.toString()) }
    var username by rememberSaveable { mutableStateOf(initial.username) }
    var password by remember { mutableStateOf("") }
    var base by rememberSaveable { mutableStateOf(initial.base) }
    val config = BrokerConfig(host, port.toIntOrNull() ?: 0, username, password, base)

    Column(Modifier.fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState()).padding(24.dp)) {
        Text("Connect to the cooler", style = MaterialTheme.typography.headlineSmall)
        Text("TLS only; the password is stored encrypted on this phone.", style = MaterialTheme.typography.bodySmall)
        OutlinedTextField(host, { host = it }, label = { Text("Broker host") }, singleLine = true,
            modifier = Modifier.fillMaxWidth().padding(top = 16.dp))
        OutlinedTextField(port, { port = it.filter(Char::isDigit) }, label = { Text("Port") }, singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
        OutlinedTextField(username, { username = it }, label = { Text("Username") }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        OutlinedTextField(password, { password = it }, label = { Text("Password") }, singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth())
        OutlinedTextField(base, { base = it }, label = { Text("Topic base") }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        config.problems.firstOrNull()?.let {
            Text(it, color = CoolerColors.Muted, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 8.dp))
        }
        Button(
            onClick = { onTestAndSave(config) },
            enabled = config.isUsable && !setup.probing,
            modifier = Modifier.fillMaxWidth().padding(top = 16.dp),
        ) {
            if (setup.probing) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.width(8.dp))
                    Text("Testing…")
                }
            } else {
                Text("Test & save")
            }
        }
        setup.error?.let { Text(it, color = CoolerColors.Bad, modifier = Modifier.padding(top = 8.dp)) }
        if (onCancel != null) TextButton(onClick = onCancel, enabled = !setup.probing, modifier = Modifier.fillMaxWidth()) { Text("Cancel") }
    }
}
