package jnirt;

// Stands in for android/src/main/kotlin/jnirt/NativePeerNatives.kt on the host: the one native method that jnirt
// registers for peers.
public class NativePeerNatives {
    public static native void close(long handle);
}
