package jnirt

import androidx.annotation.RequiresApi
import java.lang.ref.Cleaner
import java.lang.ref.PhantomReference
import java.lang.ref.ReferenceQueue
import java.util.Collections
import java.util.concurrent.ConcurrentHashMap

/**
 * The safety net of [NativePeer] (ADR-0009 §6): closes the native object of a peer that was forgotten without
 * `close()`, some time after the peer became unreachable. It is a net, not a plan: when it runs is up to the
 * garbage collector, and it may not run before the process ends.
 *
 * The [state] is all the guard may keep. Keeping [owner] itself would make it reachable for ever.
 */
interface LeakGuard {

    fun watch(owner: Any, state: PeerState)
}

object LeakGuards {

    /**
     * Set to `true` in a debug build (`LeakGuards.warnOnLeak = BuildConfig.DEBUG`) to hear about a peer that was
     * not closed. Off by default: a release build closes it silently.
     * */
    @Volatile
    @JvmStatic
    var warnOnLeak: Boolean = false

    /**
     * Where the warning goes; replace it to use Timber. The text names the class.
     * */
    @Volatile
    @JvmStatic
    var warning: (String) -> Unit = { System.err.println(it) }

    @Suppress("NewApi") // hasCleaner() is the API check
    internal fun default(): LeakGuard =
        if (hasCleaner()) CleanerLeakGuard() else ReferenceQueueLeakGuard()

    // `java.lang.ref.Cleaner` exists on Android 13 (API 33) and on a JDK 9+, which is what host tests run on.
    private fun hasCleaner(): Boolean = try {
        Class.forName("java.lang.ref.Cleaner")
        true
    } catch (e: ClassNotFoundException) {
        false
    }

    internal fun closeForgotten(className: String, state: PeerState) {
        if (state.release() && warnOnLeak) {
            warning("$className was not closed: call close() (use { } does it). The leak guard closed it.")
        }
    }
}

/**
 * [LeakGuard] on `java.lang.ref.Cleaner`.
 */
@RequiresApi(33)
internal class CleanerLeakGuard : LeakGuard {

    private val cleaner = Cleaner.create()

    override fun watch(owner: Any, state: PeerState) {
        cleaner.register(owner, ForgottenPeer(owner.javaClass.simpleName, state))
    }

    // Must not be able to reach the owner: a plain class with the two things it needs.
    private class ForgottenPeer(private val className: String, private val state: PeerState) : Runnable {
        override fun run() = LeakGuards.closeForgotten(className, state)
    }
}

/**
 * [LeakGuard] for API 26 to 32, which have no `Cleaner`: a `PhantomReference` per peer and one daemon thread that waits
 * for the garbage collector to enqueue them.
 */
internal class ReferenceQueueLeakGuard : LeakGuard {

    private val queue = ReferenceQueue<Any>()

    // The references themselves must stay reachable until they are enqueued.
    private val pending: MutableSet<Forgotten> = Collections.newSetFromMap(ConcurrentHashMap())

    private val thread: Thread by lazy {
        Thread({ drain() }, "jnirt-leak-guard").apply {
            isDaemon = true
            start()
        }
    }

    override fun watch(owner: Any, state: PeerState) {
        pending.add(Forgotten(owner, queue, owner.javaClass.simpleName, state))
        thread // starts the thread on first use
    }

    private fun drain() {
        while (true) {
            val forgotten = try {
                queue.remove() as Forgotten
            } catch (e: InterruptedException) {
                return
            }
            pending.remove(forgotten)
            LeakGuards.closeForgotten(forgotten.className, forgotten.state)
        }
    }

    private class Forgotten(
        owner: Any,
        queue: ReferenceQueue<Any>,
        val className: String,
        val state: PeerState,
    ) : PhantomReference<Any>(owner, queue)
}
