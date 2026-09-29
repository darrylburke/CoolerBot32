package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.LinkStatus
import ai.northtrail.cooler.model.Publish
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

data class IncomingMessage(val topic: String, val payload: String, val retained: Boolean)

interface CoolerTransport {
    val link: StateFlow<LinkStatus>
    val messages: SharedFlow<IncomingMessage>
    fun connect()
    fun disconnect()

    /** Returns false if the message could not be handed to a connected client. */
    fun publish(publish: Publish): Boolean
}
