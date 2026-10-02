package jnirt

/**
 * Ties native memory to the `ByteBuffer` that shows it to Kotlin (ADR-0009 §9). The native side calls [watch] when a
 * native function returns a `NativeBuffer`; from then on the memory is released when the garbage collector collects
 * the buffer, by the same [LeakGuard] that watches forgotten peers. Nothing in Kotlin calls this.
 *
 * R8 must keep this class: see consumer-rules.pro.
 */
object NativeBuffers {

    // Called from native code. `buffer` is a java.nio.ByteBuffer; `handle` is the memory in the table of peers.
    @JvmStatic
    private fun watch(buffer: Any, handle: Long) {
        PeerRuntime.guard.watch(buffer, PeerState(handle, quiet = true))
    }
}
