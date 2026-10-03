package ai.northtrail.cooler.data

import java.io.InputStream
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

/**
 * Just enough of an MQTT 3.1.1 broker for tests: answers CONNECT with [connackCode],
 * records SUBSCRIBE topic filters, grants them (or refuses those in [refuse]), and optionally publishes one
 * message (QoS 0) right after the SUBACK.
 */
class FakeBroker(
    private val connackCode: Int = 0,
    private val publishAfterSubscribe: Pair<String, String>? = null,
    private val refuse: Set<String> = emptySet(),
) : AutoCloseable {
    private val server = ServerSocket(0)
    val port: Int get() = server.localPort
    val accepted = AtomicInteger()
    val subscribedTopics = CopyOnWriteArrayList<String>()

    init {
        thread(isDaemon = true) {
            while (!server.isClosed) {
                val socket = runCatching { server.accept() }.getOrNull() ?: break
                accepted.incrementAndGet()
                thread(isDaemon = true) { runCatching { serve(socket) } }
            }
        }
    }

    private fun serve(socket: Socket) = socket.use { s ->
        val input = s.getInputStream()
        val out = s.getOutputStream()
        readPacket(input) ?: return@use // CONNECT
        out.write(byteArrayOf(0x20, 0x02, 0x00, connackCode.toByte()))
        out.flush()
        if (connackCode != 0) {
            Thread.sleep(300)
            return@use
        }
        while (true) {
            val (type, body) = readPacket(input) ?: break
            when (type) {
                0x80 -> { // SUBSCRIBE: packet id, then (length, topic, qos)*
                    val topics = mutableListOf<String>()
                    var i = 2
                    while (i + 2 <= body.size) {
                        val len = ((body[i].toInt() and 0xFF) shl 8) or (body[i + 1].toInt() and 0xFF)
                        topics += String(body, i + 2, len, Charsets.UTF_8)
                        i += 2 + len + 1
                    }
                    subscribedTopics += topics
                    out.write(byteArrayOf(0x90.toByte(), (2 + topics.size).toByte(), body[0], body[1]))
                    out.write(ByteArray(topics.size) { if (topics[it] in refuse) 0x80.toByte() else 0x01 })
                    publishAfterSubscribe?.let { (topic, payload) -> out.write(publishPacket(topic, payload)) }
                }
                0xC0 -> out.write(byteArrayOf(0xD0.toByte(), 0x00)) // PINGREQ -> PINGRESP
                0xE0 -> break // DISCONNECT
            }
            out.flush()
        }
    }

    private fun readPacket(input: InputStream): Pair<Int, ByteArray>? {
        val header = input.read()
        if (header < 0) return null
        var length = 0
        var multiplier = 1
        while (true) {
            val b = input.read()
            if (b < 0) return null
            length += (b and 0x7F) * multiplier
            if (b and 0x80 == 0) break
            multiplier *= 128
        }
        val body = ByteArray(length)
        var off = 0
        while (off < length) {
            val r = input.read(body, off, length - off)
            if (r < 0) return null
            off += r
        }
        return (header and 0xF0) to body
    }

    private fun publishPacket(topic: String, payload: String): ByteArray {
        val t = topic.toByteArray()
        val body = byteArrayOf((t.size shr 8).toByte(), t.size.toByte()) + t + payload.toByteArray()
        return byteArrayOf(0x30) + remainingLength(body.size) + body
    }

    private fun remainingLength(n: Int): ByteArray {
        val out = mutableListOf<Byte>()
        var x = n
        do {
            var b = x % 128
            x /= 128
            if (x > 0) b = b or 0x80
            out += b.toByte()
        } while (x > 0)
        return out.toByteArray()
    }

    override fun close() = server.close()
}

fun waitUntil(timeoutMs: Long = 5_000, condition: () -> Boolean) {
    val end = System.currentTimeMillis() + timeoutMs
    while (!condition() && System.currentTimeMillis() < end) Thread.sleep(20)
}
