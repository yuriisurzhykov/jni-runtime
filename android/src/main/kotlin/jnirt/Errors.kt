package jnirt

/**
 * How much an error can be cured, the same for every library. An application usually branches on this and looks at
 * the exact code only when it wants a special reaction.
 */
enum class ErrorKind {
    /** Trying again later can help: network, timeout. */
    Transient,

    /** A person has to do something: a password, a permission, free space. */
    NeedsUser,

    /** Trying again will not help. */
    Fatal,
}

/**
 * The error enum of a library: `enum class CameraError(...) : LibraryError`. Keep it small: a value exists only if
 * a caller reacts to it differently.
 */
interface LibraryError {
    val kind: ErrorKind
}

// The domain of the C++ jnirt::Errc.
private const val RUNTIME_DOMAIN = "jnirt"

/**
 * The common errors of the native runtime: the C++ `jnirt::Errc` (domain `"jnirt"`). Any library may throw them, so
 * every [ErrorDomain] says where they go in its own enum.
 */
enum class JnirtError(val code: Int) {
    Timeout(1),
    NotFound(2),
    Io(3),
    Internal(4),
    Unknown(-1);

    companion object {
        fun fromCode(code: Int): JnirtError = entries.firstOrNull { it.code == code } ?: Unknown
    }
}

/**
 * Connects a library's error enum to its C++ domain. Implement it in the companion object of the enum:
 *
 * ```
 * enum class CameraError(val code: Int, override val kind: ErrorKind) : LibraryError {
 *     AuthFailed(1, ErrorKind.NeedsUser), Timeout(3, ErrorKind.Transient), Unknown(-1, ErrorKind.Fatal);
 *
 *     companion object : ErrorDomain<CameraError> {
 *         override val domain = "wificameras"
 *         override fun fromCode(code: Int) = entries.firstOrNull { it.code == code } ?: Unknown
 *         override fun fromRuntime(error: JnirtError) = if (error == JnirtError.Timeout) Timeout else Unknown
 *     }
 * }
 * ```
 */
interface ErrorDomain<E : LibraryError> {
    /** The string of `JNIRT_ERROR_DOMAIN` in the library's C++. */
    val domain: String

    /** A number of this domain to the enum value. A number nobody knows (a newer native library) is `Unknown`. */
    fun fromCode(code: Int): E

    /** A common error of the runtime to the value of this library that stands for it. */
    fun fromRuntime(error: JnirtError): E

    /** The error of [e] in this library's terms: its own, a common one, or `null` for another library's. */
    fun map(e: NativeException): E? = when (e.domain) {
        domain -> fromCode(e.code)
        RUNTIME_DOMAIN -> fromRuntime(JnirtError.fromCode(e.code))
        else -> null
    }
}

/** [ErrorDomain.map] as an extension: `e.asError(CameraError)`. */
fun <E : LibraryError> NativeException.asError(domain: ErrorDomain<E>): E? = domain.map(this)
