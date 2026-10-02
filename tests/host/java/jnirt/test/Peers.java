package jnirt.test;

// The natives of peer_convert_test.cpp: a native object behind a Long, as NativePeer will use it. All static, because
// the natives take the handle as a parameter (ADR-0009 §5).
public class Peers {
    public static native long open(String name);

    public static native long openFailing();

    public static native long openEmpty();

    public static native String nameOf(long handle);

    public static native String nameOfOther(long handle);

    // Blocks in native code until the peer is closed, then throws IllegalStateException.
    public static native void block(long handle);
}
