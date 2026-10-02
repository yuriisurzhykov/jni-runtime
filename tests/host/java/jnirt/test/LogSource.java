package jnirt.test;

import jnirt.NativeLogBridge;

// The class of the log forwarding tests: a native that logs a line, and helpers that set up the stand-in receiver
// (jnirt.NativeLogBridge) from the C++ side.
public class LogSource {
    public static native void logLine(String text);

    public static native void logWhilePending(String text);

    public static native void logInCritical(int[] values);

    public static void reset() {
        NativeLogBridge.reset();
    }

    public static String received() {
        return NativeLogBridge.all();
    }

    public static void receiverThrows(boolean on) {
        NativeLogBridge.throwOnLog = on;
    }

    // the receiver logs `text` into native while it handles the next line
    public static void logInsideReceiver(String text) {
        NativeLogBridge.duringLog = () -> logLine(text);
    }

    public static void setLevel(int priority) {
        NativeLogBridge.setNativeLevel(priority);
    }

    public static String probePending(String text) {
        try {
            logWhilePending(text);
            return "no exception";
        } catch (Throwable t) {
            return t.getClass().getName() + ": " + t.getMessage();
        }
    }
}
