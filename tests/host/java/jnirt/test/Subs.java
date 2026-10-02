package jnirt.test;

// The natives of listener_test.cpp: a native source of events (`open`) and Java listeners subscribed to it.
public class Subs {
    public static native long open();

    // The handle of the subscription (a peer).
    public static native long subscribe(long source, Sink sink);

    public static native void emit(long source, String text);

    public static native void emitFrame(long source, int size, int fill);

    // `count` events from a thread of its own; returns the number of local references that thread still held at the end.
    public static native int emitFromThread(long source, int count);

    public static Recorder newRecorder() {
        return new Recorder(false);
    }

    public static Recorder failingRecorder() {
        return new Recorder(true);
    }

    public static void gc() {
        System.gc();
    }
}
