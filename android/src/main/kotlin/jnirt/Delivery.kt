package jnirt

import java.util.concurrent.Executor
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

/**
 * Where the events of one subscription are delivered (ADR-0010 §3). Native code calls the listener on its own thread
 * (a camera thread, say); a library wraps its listener so that the call goes through here instead:
 *
 * ```
 * fun subscribe(executor: Executor, listener: FrameListener): Subscription {
 *     val delivery = Delivery(executor)                       // the executor is captured HERE, at subscription time
 *     val forNative = object : FrameListener {                // this object is what native code calls
 *         override fun onFrame(frame: ByteBuffer) = delivery.post { listener.onFrame(frame) }
 *     }
 *     return Subscription(this, call { nativeSubscribe(it, forNative) }, delivery)
 * }
 * ```
 *
 * [post] never waits and never runs the block on the calling thread. After [shutdown] (what `Subscription.close()` does
 * first) a block that was waiting in the executor is dropped when its turn comes. So on the thread of a serial executor
 * (the main thread, a `Handler`, a single-thread executor) nothing runs after `close()` returned there; from another
 * thread a block that is already running may finish later.
 *
 * Exceptions of the block are not caught: with a `Handler` they crash the app like any bug in a callback. With a direct
 * executor (`Executor { it.run() }`, which runs on the native thread and must be asked for explicitly) they reach
 * native code, which logs them.
 */
class Delivery(private val executor: Executor) {

    private val open = AtomicBoolean(true)
    private val pending = AtomicReference<(() -> Unit)?>(null)
    private val scheduled = AtomicBoolean(false)

    /**
     * Runs [block] on the executor, unless the subscription is closed by then. Every event is delivered, in the order of
     * the executor.
     * */
    fun post(block: () -> Unit) {
        if (!open.get()) return
        executor.execute { if (open.get()) block() }
    }

    /**
     * Like [post], but for a stream where only the newest event matters (video frames): if the executor is busy, an
     * event that has not run yet is replaced by the next one (Kotlin's `Flow.conflate()`). At most one block is waiting.
     * */
    fun postLatest(block: () -> Unit) {
        if (!open.get()) return
        pending.set(block)
        if (scheduled.compareAndSet(false, true)) {
            executor.execute {
                scheduled.set(false)
                val latest = pending.getAndSet(null)
                if (latest != null && open.get()) latest()
            }
        }
    }

    /**
     * No block runs from now on, also those already waiting in the executor.
     * */
    fun shutdown() {
        open.set(false)
        pending.set(null)
    }
}
