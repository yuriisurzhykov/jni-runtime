package jnirt

/**
 * The result of a library's public call: a value or the library's own error. The application reads it with an
 * exhaustive `when` and never sees [NativeException]:
 *
 * ```
 * when (val r = camera.grab(500)) {
 *     is Outcome.Success -> show(r.value)
 *     is Outcome.Failure -> if (r.error.kind == ErrorKind.Transient) retry() else showError(r.error)
 * }
 * ```
 *
 * Programmer errors (`IllegalArgumentException`, `IllegalStateException`) are not turned into an [Outcome]: they are
 * bugs to fix, not situations to handle.
 */
sealed interface Outcome<out T, out E> {
    class Success<out T>(val value: T) : Outcome<T, Nothing>
    class Failure<out E>(val error: E) : Outcome<Nothing, E>
}

/**
 * Calls native code and turns its [NativeException] into the library's error. This is the one place where a library
 * does the mapping:
 *
 * ```
 * fun grab(timeoutMs: Int): Outcome<Frame, CameraError> = nativeCall(CameraError) { nativeGrab(timeoutMs) }
 * ```
 *
 * An error of another library is not this library's to map: it is thrown again unchanged.
 */
inline fun <T, E : LibraryError> nativeCall(domain: ErrorDomain<E>, block: () -> T): Outcome<T, E> =
    try {
        Outcome.Success(block())
    } catch (e: NativeException) {
        Outcome.Failure(domain.map(e) ?: throw e)
    }
