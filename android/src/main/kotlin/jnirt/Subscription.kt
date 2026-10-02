package jnirt

/**
 * One registered listener (ADR-0010): closing it unregisters the listener. It is a peer, so `close()` is idempotent,
 * never waits, and works with `use { }`.
 *
 * What `close()` guarantees: no new call of the listener starts, and the events still waiting in the [Delivery] are
 * dropped. On the thread of a serial executor that means no call at all after `close()` returned; from another thread a
 * call that is already running may finish later. `close()` from inside the listener itself is fine.
 *
 * Closing the [owner] closes its subscriptions. The owner keeps a subscription only until it is closed, so a
 * subscription that was forgotten is not collected while its owner lives, and its listener stays subscribed: as with
 * any listener in Android, unsubscribe in `onStop` / `onDestroy`.
 *
 * Made by the library's `subscribe`, not by the app: see [Delivery].
 */
class Subscription internal constructor(
    private val owner: NativePeer,
    handle: Long,
    private val delivery: Delivery,
) : NativePeer(handle) {

    init {
        owner.adopt(this)   // an owner that is already closed closes it at once
    }

    override fun close() {
        delivery.shutdown()  // first the flag, then the native unregistration
        owner.forget(this)
        super.close()
    }
}
