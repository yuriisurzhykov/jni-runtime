package jnirt

/**
 * What native code throws for a recoverable error (`throw jnirt::Error(Errc::X, "text")` in C++).
 *
 * It is one class for every library: [domain] says which library (the string of `JNIRT_ERROR_DOMAIN`) and [code] says
 * which error (the number in that library's C++ `enum class`). There are no subclasses on purpose: a library maps this
 * exception once into its own `enum class` and returns an [Outcome], see [nativeCall].
 *
 * The native side creates it through this constructor, so R8 must keep it: see consumer-rules.pro.
 */
class NativeException(
    val domain: String,
    val code: Int,
    message: String?,
    cause: Throwable? = null,
) : RuntimeException(message, cause) {

    override fun toString(): String = "NativeException[$domain/$code]: $message"
}
