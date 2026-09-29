package ai.northtrail.cooler.data

import ai.northtrail.cooler.model.LinkState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.net.ConnectException
import java.security.cert.CertificateException
import javax.net.ssl.SSLException
import javax.net.ssl.SSLHandshakeException
import javax.net.ssl.SSLPeerUnverifiedException

class FatalReasonTest {
    private fun state(error: Throwable) = fatal(error)?.first

    @Test
    fun peerUnverifiedIsFatalTlsEvenWhenWrapped() {
        assertEquals(LinkState.TLS_FAILED, state(RuntimeException(SSLPeerUnverifiedException("x"))))
    }

    @Test
    fun certificateExceptionIsFatalTls() {
        assertEquals(LinkState.TLS_FAILED, state(CertificateException("x")))
    }

    @Test
    fun handshakeExceptionIsFatalTls() {
        assertEquals(LinkState.TLS_FAILED, state(SSLHandshakeException("x")))
    }

    @Test
    fun plainSslExceptionStillRetries() {
        assertNull(state(SSLException("Connection reset")))
    }

    @Test
    fun connectExceptionStillRetries() {
        assertNull(state(ConnectException("refused")))
    }
}
