package jnirt.test;

// The natives of peer_convert_test.cpp: a native object behind a Long, as NativePeer will use it. All static, because
// the natives take the handle as a parameter (ADR-0009 §5).
public class Peers {
    public static native long open(String name);

    public static native long openFailing();

    public static native long openEmpty();

    public static native String nameOf(long handle);

    public static native String nameOfOther(long handle);

    // A direct ByteBuffer over native memory: `size` bytes, each byte `fill`.
    public static native java.nio.ByteBuffer frame(int size, int fill);

    // The same, taken from the camera `handle` (its name's bytes).
    public static native java.nio.ByteBuffer nameBytes(long handle);

    // A frame whose memory is counted: alive() says how many such frames are alive in native code.
    public static native java.nio.ByteBuffer countedFrame();

    public static native int countedAlive();

    public static native java.nio.ByteBuffer emptyFrame();

    public static void gc() {
        System.gc();
    }

    // Asks for garbage collection until `done` says the native memory is gone, at most about 10 seconds.
    public static boolean collectUntilGone() throws InterruptedException {
        for (int i = 0; i < 200; i++) {
            System.gc();
            if (countedAlive() == 0) return true;
            Thread.sleep(50);
        }
        return false;
    }

    // Blocks in native code until the peer is closed, then throws IllegalStateException.
    public static native void block(long handle);
}
