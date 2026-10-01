package jnirt

import java.lang.reflect.Modifier

/**
 * The one place that loads the native library, and the Kotlin half of the startup check (ADR-0006).
 *
 * The native side reads [VERSION] and calls [nativeMethodNames] from `jnirt::init` in JNI_OnLoad. Both are kept by
 * consumer-rules.pro: R8 sees no Kotlin caller for them.
 */
object NativeLoader {

    /**
     * Must equal the version of the native runtime (the CMake project version); init fails with JNI_ERR if not.
     * */
    const val VERSION = "0.1.0"

    /**
     * `System.loadLibrary`, with a pointer to the log when the startup check of `jnirt::init` failed.
     * */
    @JvmStatic
    fun load(name: String) {
        try {
            System.loadLibrary(name)
        } catch (e: UnsatisfiedLinkError) {
            throw UnsatisfiedLinkError("${e.message}. If JNI_OnLoad failed, the logcat tag 'jnirt' lists every problem.")
        }
    }

    /**
     * The names of the `external fun` of [cls], comma separated: native code checks that it registered each one.
     * */
    @JvmStatic
    fun nativeMethodNames(cls: Class<*>): String =
        cls.declaredMethods.filter { Modifier.isNative(it.modifiers) }.joinToString(",") { it.name }
}
