package jnirt

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

private enum class CameraError(val code: Int, override val kind: ErrorKind) : LibraryError {
    AuthFailed(1, ErrorKind.NeedsUser),
    StreamLost(2, ErrorKind.Transient),
    Timeout(3, ErrorKind.Transient),
    Unknown(-1, ErrorKind.Fatal);

    companion object : ErrorDomain<CameraError> {
        override val domain = "wificameras"
        override fun fromCode(code: Int) = entries.firstOrNull { it.code == code } ?: Unknown
        override fun fromRuntime(error: JnirtError) = if (error == JnirtError.Timeout) Timeout else Unknown
    }
}

class NativeCallTest {

    private fun failing(domain: String, code: Int): Nothing = throw NativeException(domain, code, "text")

    @Test
    fun `a value comes back as Success`() {
        val r = nativeCall(CameraError) { 42 }
        assertEquals(42, (r as Outcome.Success).value)
    }

    @Test
    fun `an error of the own domain becomes the library enum`() {
        val r = nativeCall(CameraError) { failing("wificameras", 1) }
        val error = (r as Outcome.Failure).error
        assertEquals(CameraError.AuthFailed, error)
        assertEquals(ErrorKind.NeedsUser, error.kind)
    }

    @Test
    fun `a common runtime error goes where the library says`() {
        val timeout = nativeCall(CameraError) { failing("jnirt", JnirtError.Timeout.code) }
        assertEquals(CameraError.Timeout, (timeout as Outcome.Failure).error)

        val other = nativeCall(CameraError) { failing("jnirt", JnirtError.Io.code) }
        assertEquals(CameraError.Unknown, (other as Outcome.Failure).error)
    }

    @Test
    fun `a code nobody knows is Unknown`() {
        val r = nativeCall(CameraError) { failing("wificameras", 99) }
        assertEquals(CameraError.Unknown, (r as Outcome.Failure).error)
    }

    @Test
    fun `an error of another library is thrown again unchanged`() {
        val original = NativeException("sync", 2, "quota")
        try {
            nativeCall(CameraError) { throw original }
            fail("not thrown")
        } catch (e: NativeException) {
            assertSame(original, e)
        }
    }

    @Test
    fun `programmer errors are not turned into an Outcome`() {
        try {
            nativeCall(CameraError) { throw IllegalArgumentException("bad") }
            fail("not thrown")
        } catch (e: IllegalArgumentException) {
            assertEquals("bad", e.message)
        }
    }

    @Test
    fun `asError maps one exception`() {
        val e = NativeException("wificameras", 2, "lost")
        assertEquals(CameraError.StreamLost, e.asError(CameraError))
        assertEquals(null, NativeException("sync", 1, "x").asError(CameraError))
    }

    @Test
    fun `NativeException keeps the cause`() {
        val cause = java.io.IOException("disk")
        val e = NativeException("jnirt", 3, "write failed", cause)
        assertSame(cause, e.cause)
        assertTrue(e.toString().startsWith("NativeException[jnirt/3]"))
    }

    @Test
    fun `JnirtError fromCode matches the C++ Errc`() {
        assertEquals(JnirtError.Timeout, JnirtError.fromCode(1))
        assertEquals(JnirtError.NotFound, JnirtError.fromCode(2))
        assertEquals(JnirtError.Io, JnirtError.fromCode(3))
        assertEquals(JnirtError.Internal, JnirtError.fromCode(4))
        assertEquals(JnirtError.Unknown, JnirtError.fromCode(77))
    }
}
