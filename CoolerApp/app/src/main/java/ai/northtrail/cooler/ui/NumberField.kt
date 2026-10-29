package ai.northtrail.cooler.ui

import ai.northtrail.cooler.model.TypedEntry
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp

/**
 * A number setting row: label, value, − and +. Tapping the value (when [enabled])
 * asks to [onOpen] a typed field; while [open], a field and Send replace the value,
 * and Send is enabled only for a whole number in [lo]..[hi]. Back or focus leaving
 * the field calls [onClose] without sending. Test tags use [tag].
 */
@Composable
fun SteppedValue(
    label: String,
    valueText: String,
    pending: Boolean,
    lo: Int,
    hi: Int,
    value: Int?,
    enabled: Boolean,
    open: Boolean,
    onOpen: () -> Unit,
    onClose: () -> Unit,
    onSend: (Int) -> Unit,
    onStep: (Int) -> Unit,
    tag: String,
    note: String? = null,
) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
        Row(Modifier.fillMaxWidth().heightIn(min = 56.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(label, Modifier.weight(1f))
            if (open && enabled) {
                TypedField(lo, hi, onClose, onSend, tag)
            } else {
                if (pending) CircularProgressIndicator(Modifier.size(16.dp).testTag("pending-$tag"), strokeWidth = 2.dp)
                Text(
                    valueText,
                    color = if (pending) CoolerColors.Muted else CoolerColors.Text,
                    modifier = Modifier
                        .clickable(enabled = enabled && value != null, onClickLabel = "Type $label", onClick = onOpen)
                        .padding(horizontal = 12.dp, vertical = 8.dp)
                        .testTag("value-$tag"),
                )
                IconButton(onClick = { onStep(-1) }, enabled = enabled && value != null && value > lo) {
                    Icon(Icons.Filled.Remove, contentDescription = "Lower $label")
                }
                IconButton(onClick = { onStep(+1) }, enabled = enabled && value != null && value < hi) {
                    Icon(Icons.Filled.Add, contentDescription = "Raise $label")
                }
            }
        }
        if (open && enabled) {
            Text(
                TypedEntry.hint(lo, hi) + (note?.let { "  ·  $it" } ?: ""),
                color = CoolerColors.Muted,
                style = MaterialTheme.typography.labelSmall,
                modifier = Modifier.padding(bottom = 4.dp),
            )
        } else if (note != null) {
            Text(note, color = CoolerColors.Muted, style = MaterialTheme.typography.labelSmall)
        }
    }
}

@Composable
private fun TypedField(lo: Int, hi: Int, onClose: () -> Unit, onSend: (Int) -> Unit, tag: String) {
    var text by rememberSaveable { mutableStateOf("") }
    val parsed = TypedEntry.parse(text, lo, hi)
    val focus = remember { FocusRequester() }
    var hadFocus by remember { mutableStateOf(false) }
    BackHandler(onBack = onClose)
    LaunchedEffect(Unit) { focus.requestFocus() }
    val send = { parsed?.let { onSend(it); onClose() } }
    OutlinedTextField(
        value = text,
        onValueChange = { text = it.take(6) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Send),
        keyboardActions = KeyboardActions(onSend = { send() }),
        isError = text.isNotBlank() && parsed == null,
        modifier = Modifier
            .width(110.dp)
            .focusRequester(focus)
            .onFocusChanged {
                if (it.isFocused) hadFocus = true else if (hadFocus) onClose()
            }
            .testTag("field-$tag"),
    )
    TextButton(onClick = { send() }, enabled = parsed != null, modifier = Modifier.testTag("send-$tag")) {
        Text("Send")
    }
}
