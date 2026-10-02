package jnirt

/**
 * The one native method behind [NativePeer.close]. jnirt registers it itself in `jnirt::init`, a library does not
 * write it. R8 must keep this class: see consumer-rules.pro.
 */
object NativePeerNatives {

    /**
     * Closes the peer with this handle: its native object is told to stop at once and is destroyed after the last
     * call in flight. A handle that is closed already, or never existed, does nothing.
     * */
    @JvmStatic
    external fun close(handle: Long)
}
