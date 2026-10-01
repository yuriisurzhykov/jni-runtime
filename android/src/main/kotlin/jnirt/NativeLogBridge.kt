package jnirt

import android.util.Log
import jnirt.NativeLogBridge.log

/**
 * Where the log lines of native code arrive. C++ `LOGI(...)` calls [log] for every line that passes the level
 * filter. Set a [receiver] once to put them where the Kotlin logs go:
 *
 * ```
 * NativeLogBridge.receiver = NativeLogBridge.Receiver { priority, tag, message ->
 *     Timber.tag(tag).log(priority, message)
 * }
 * ```
 *
 * Without a receiver the line is written to `android.util.Log`, as native code did before. A line goes to exactly
 * one place. Native code only calls Kotlin after `JNI_OnLoad` succeeded; a thread that the JVM does not know, and
 * code that runs while an exception is pending, write to logcat directly.
 *
 * The native side calls [log] by name and registers [setNativeLevel] itself, so R8 must keep this class: see
 * consumer-rules.pro.
 */
object NativeLogBridge {

    fun interface Receiver {

        /**
         * [priority] is a constant of `android.util.Log` (VERBOSE 2 ... ERROR 6).
         * */
        fun log(priority: Int, tag: String, message: String)
    }

    /**
     * Where native lines go; `null` means `android.util.Log`.
     * */
    @Volatile
    @JvmStatic
    var receiver: Receiver? = null

    /**
     * The lowest priority native code still logs (an `android.util.Log` constant). Needs the library loaded.
     * */
    @JvmStatic
    external fun setNativeLevel(priority: Int)

    // Called from native code.
    @JvmStatic
    private fun log(priority: Int, tag: String, message: String) {
        val current = receiver
        if (current != null) current.log(priority, tag, message) else Log.println(priority, tag, message)
    }
}
