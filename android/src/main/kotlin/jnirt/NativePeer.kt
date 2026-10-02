package jnirt

import java.io.Closeable
import java.util.concurrent.atomic.AtomicLong

/**
 * A Kotlin object that owns a native object (ADR-0009). The native object is found by a [handle][call], a number that
 * is not an address: a closed or unknown handle is an `IllegalStateException`, never a crash.
 *
 * ```
 * class Camera private constructor(handle: Long) : NativePeer(handle) {
 *     fun read(): ByteArray = call { nativeRead(it) }
 *
 *     companion object {
 *         fun open(url: String) = Camera(nativeOpen(url))
 *         @JvmStatic private external fun nativeOpen(url: String): Long
 *         @JvmStatic private external fun nativeRead(handle: Long): ByteArray
 *     }
 * }
 *
 * Camera.open("rtsp://10.0.0.5/live").use { camera -> camera.read() }
 * ```
 *
 * [close] is idempotent and never waits. It tells the native object to stop at once (a blocked call in another thread
 * wakes up, like `Socket.close()`), and the native memory goes after the last call in flight. A call on a closed peer
 * throws `IllegalStateException`. A peer that was forgotten without `close()` is closed after garbage collection
 * by a [LeakGuard]; do not count on it.
 */
abstract class NativePeer protected constructor(handle: Long) : Closeable {

    private val state = PeerState(handle)

    init {
        require(handle != 0L) { "${javaClass.simpleName}: the native side gave no handle" }
        PeerRuntime.guard.watch(this, state)
    }

    /**
     * True after [close].
     * */
    val isClosed: Boolean
        get() = state.handle == 0L

    /**
     * Runs [block] with the handle, for a native call: `call { nativeRead(it) }`. Throws `IllegalStateException` when
     * the peer is closed. If [close] wins the race after this check, the native side says the same.
     * */
    protected fun <R> call(block: (handle: Long) -> R): R {
        val handle = state.handle
        check(handle != 0L) { "${javaClass.simpleName} is closed" }
        return block(handle)
    }

    /**
     * Releases the native object now. Safe to call twice and from any thread; it does not wait for calls in flight.
     * */
    override fun close() {
        state.release()
    }
}

/**
 * What is needed to close a peer, apart from the peer itself: a [LeakGuard] holds this, never the peer, or the peer
 * would never become unreachable.
 */
class PeerState internal constructor(handle: Long) {

    private val value = AtomicLong(handle)

    val handle: Long
        get() = value.get()

    /**
     * Closes the native peer once. Returns true for the caller that did it, false for every later one.
     * */
    fun release(): Boolean {
        val taken = value.getAndSet(0L)
        if (taken == 0L) return false
        PeerRuntime.release(taken)
        return true
    }
}

/**
 * The two things a test replaces: how a handle is closed, and who watches for forgotten peers.
 */
internal object PeerRuntime {

    @Volatile
    var release: (Long) -> Unit = { NativePeerNatives.close(it) }

    @Volatile
    var guard: LeakGuard = LeakGuards.default()
}
