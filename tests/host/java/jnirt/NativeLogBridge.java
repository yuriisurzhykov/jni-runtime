package jnirt;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

// Stands in for android/src/main/kotlin/jnirt/NativeLogBridge.kt on the host: the same static `log` that the native
// side calls, and the same native `setNativeLevel`. Instead of a receiver it collects the lines.
public class NativeLogBridge {
    public static final List<String> lines = Collections.synchronizedList(new ArrayList<>());

    // a test makes the receiver fail, or log into native from inside the receiver
    public static boolean throwOnLog;
    public static Runnable duringLog;

    public static native void setNativeLevel(int priority);

    private static void log(int priority, String tag, String message) {
        if (throwOnLog) throw new IllegalStateException("receiver broke");
        lines.add(priority + "/" + tag + "/" + message);
        Runnable inside = duringLog;
        if (inside != null) {
            duringLog = null;
            inside.run();
        }
    }

    public static void reset() {
        lines.clear();
        throwOnLog = false;
        duringLog = null;
    }

    public static String all() {
        synchronized (lines) {
            return String.join("\n", lines);
        }
    }
}
