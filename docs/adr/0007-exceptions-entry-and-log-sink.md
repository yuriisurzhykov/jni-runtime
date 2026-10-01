# ADR-0007 - Exceptions, entry() and the Java log sink

## Status

Accepted. Implemented in roadmap P4. Explained with examples:
[learning/exceptions-and-logs-by-example.md](../learning/exceptions-and-logs-by-example.md). Applies the contract of
[ADR-0002](0002-errors-and-asserts.md) §4–§5 and the `JavaException` of [ADR-0006](0006-class-cache-and-registry.md) §4.

## Context

ADR-0001 §6.1 says a C++ exception never crosses the JNI boundary. Until now nothing enforced it: a native function that
threw was a crash, and a Java exception thrown by a call from native code was a `JavaException` that nobody handed back
to Kotlin. Native logs went to logcat only, so they never reached Timber or the app's `FileTree`.

Most of the design was fixed earlier (ADR-0002 §4–§5, ADR-0006 §4–§5). This ADR is what remains: how `entry()` reaches
every native function, the Kotlin API for errors, the check of error domains, and how log lines travel to Kotlin.

## Decision

### 1. `entry()` is part of `native<&fn>`, not something to write

`native<&fn>("name")` registers a wrapper of `fn` with `RegisterNatives`, not `fn`. The wrapper has the same parameters
as `fn` and is, in Kotlin terms, a `try/catch` around the whole body. The call site of ADR-0006 does not change, there is
nothing to call and nothing to forget. Every native is registered through `native<>`, so "every native goes through
`entry()`" holds by construction.

```cpp
jboolean open(JNIEnv* env, jobject thiz, jstring url) {
    JNIRT_REQUIRE(url != nullptr, "url is null");                              // -> IllegalArgumentException
    if (!connect(url)) throw jnirt::Error(Errc::Io, "no route to %s", host);   // -> NativeException
    return JNI_TRUE;
}
r.add<CameraJ>().native<&open>("open");
```

Rejected: a public `jnirt::entry(env, [&]{ ... })` in every function (the early sketch). It shows what happens, but a
forgotten wrapper lets a C++ exception reach the JVM, and each native gets a lambda and an indent for nothing.

After an exception the wrapper returns `0` / `false` / `nullptr`; Java never reads it. A native that returns normally is
untouched, and a Java exception that raw JNI code left pending on a normal return is passed on as it is.

### 2. What Kotlin gets

| Caught                          | Java                                                                  | Logged                   |
|---------------------------------|-----------------------------------------------------------------------|--------------------------|
| `JavaException`                 | the original `Throwable`, the same object                              | no (not our error)       |
| `IllegalArgument`               | `IllegalArgumentException("text (file.cpp:42)")`                       | W, `IllegalArgumentException: ...` |
| `IllegalState`                  | `IllegalStateException("text (file.cpp:42)")`                          | W                        |
| `jnirt::Error`                  | `NativeException(domain, code, "text")`                                | W, `NativeException domain/code: ...` |
| `std::bad_alloc`                | `OutOfMemoryError`                                                    | E                        |
| other `std::exception`          | `NativeException("jnirt", Internal, "<C++ type>: what()")`             | E                        |
| anything else (`throw 42`)      | `NativeException("jnirt", Internal, "unknown C++ exception")`          | E                        |

`JNIRT_ASSERT` and `JNIRT_FATAL` are not exceptions and are not caught.

- **The place in the message.** For the two programmer errors the file name (no directory) and line of the
  `JNIRT_REQUIRE` / `JNIRT_CHECK` are appended: `... (camera.cpp:42)`. A native stack is not part of a Java stack trace
  (ADR-0001 §6.6), and this is what a crash report shows. Absolute paths would leak the build machine.
- **Order.** The line is logged first, the Java exception is thrown after: while one is pending nothing may be called, and
  the log may go to Java (§7).
- **Pending wins.** If a Java exception is already pending when a C++ one is caught (raw JNI code that did not check),
  the pending one stays and the C++ one is logged with the text `a Java exception is already pending`.
- **Never throws.** Building the Java exception can fail (out of memory, a constructor that throws). Then the Java
  exception that the JVM raised is the one thrown; if even that is impossible, a plain `java.lang.Error` pointing at
  logcat. Nothing leaves the wrapper as a C++ exception.
- The classes it needs (`NativeException`, `IllegalArgumentException`, `IllegalStateException`, `OutOfMemoryError`,
  `NativeLogBridge`) are described like any class and found by `init()` before the user's classes. A class missing there
  is a `JNI_ERR` with its name.

### 3. Cause chains

A chain made with `std::throw_with_nested` arrives as `Throwable.cause`, top to bottom, at most **8** exceptions (a
longer chain is cut, it is not an error). A `JavaException` inside a chain contributes its original `Throwable`, with the
Java cause chain it already has:

```cpp
try { CameraJ::connect(env, thiz); }                            // Java threw IOException
catch (const jnirt::JavaException&) { std::throw_with_nested(jnirt::Error(Errc::Io, "connect failed")); }
// Kotlin: NativeException[jnirt/3]: connect failed  <-  java.io.IOException: ...
```

### 4. Text crosses as UTF-16

A message is converted from UTF-8 to a Java string with `NewString` (UTF-16), not `NewStringUTF`: that one takes
*modified* UTF-8, and CheckJNI aborts on an emoji or an invalid byte, and those come from the outside world (a camera
name, a server reply). An invalid sequence (a stray byte, a cut-off character, an overlong form, a surrogate) becomes one
`U+FFFD` per sequence. The function is internal; P5 made the public conversion from it ([ADR-0008](0008-type-conversion.md) §3).

### 5. Kotlin API

```kotlin
class NativeException(val domain: String, val code: Int, message: String?, cause: Throwable? = null) : RuntimeException(message, cause)

enum class ErrorKind { Transient, NeedsUser, Fatal }
interface LibraryError { val kind: ErrorKind }
enum class JnirtError(val code: Int) { Timeout(1), NotFound(2), Io(3), Internal(4), Unknown(-1) }   // C++ jnirt::Errc

interface ErrorDomain<E : LibraryError> {
    val domain: String
    fun fromCode(code: Int): E                 // own number -> own enum (unknown -> Unknown)
    fun fromRuntime(error: JnirtError): E      // common errors: the library says where they go
    fun map(e: NativeException): E?            // own, common, or null for another library's
}
fun <E : LibraryError> NativeException.asError(domain: ErrorDomain<E>): E?

sealed interface Outcome<out T, out E> { class Success<T>(val value: T); class Failure<E>(val error: E) }
inline fun <T, E : LibraryError> nativeCall(domain: ErrorDomain<E>, block: () -> T): Outcome<T, E>
```

- No class hierarchy under `NativeException` (ADR-0002 §3): domain and code say which error it is, a library maps it
  once into its own `enum class`. `nativeCall` is that one place; an error of another library is thrown again unchanged,
  programmer errors are not touched.
- The application sees `Outcome<T, CameraError>` and an exhaustive `when`. It branches on `error.kind` by default.
- **Names.** Same rule everywhere: the prefix says where the error came from. `Java*` came from Java (`JavaException`),
  `Native*` came from native code (`NativeException`), a bare `Error` is C++ only (in Kotlin `Error` is `java.lang.Error`).
  `JnirtError` (not `RuntimeError`) is the Kotlin side of `jnirt::Errc`; `Outcome` (not `Result`) does not clash with
  `kotlin.Result`.
- R8 keeps `NativeException` (its constructor) and `NativeLogBridge` through `consumer-rules.pro`.
- This closes the roadmap P0 item "Kotlin result type". P9 (the first real library) is where its shape is tested; it
  changes before the API is frozen if it does not fit.

### 6. Error domains are checked at startup

`JNIRT_ERROR_DOMAIN(Enum, "name")` now also records the pair (`"name"`, `"Enum"`) when the library is loaded, with the same
trick as the members of ADR-0006 §1 (an `inline` static initialised at load, into a function-local list). Nothing is
added to the register functions, so it can not be forgotten. `init()` looks at the list and fails with `JNI_ERR` and one
problem per mistake:

- two different enums with one name (Kotlin receives only the string and the number, it could not tell them apart);
- the name `"jnirt"` for anything but `jnirt::Errc`;
- an empty name.

The same enum recorded twice is not a problem. Not checked: equal numbers inside one enum (C++ allows them), and the
numbers of the Kotlin enum (that would need Kotlin reflection; a candidate for the selfcheck).

### 7. Log lines to Kotlin

```
LOGI(...) -> format -> forwarder set? --yes, it took the line--> done
                                      \--no, or it declined----> logcat (as before)
```

- `support` has no `jni.h`, so it only gets a hook: `logging::setForwarder(bool (*)(Level, tag, message) noexcept)`, one
  atomic pointer. `init()` installs `forwardLogToJava` after a successful startup (so the line `startup done` is already
  forwarded).
- `forwardLogToJava` calls the static Kotlin method `NativeLogBridge.log(priority, tag, message)`. Priorities are the
  constants of `android.util.Log` (VERBOSE 2 ... ERROR 6). Kotlin gives the line to `NativeLogBridge.receiver` (the app
  sets it to Timber) or, when there is none, writes it to `android.util.Log`. So native always calls Kotlin and Kotlin
  decides; native has no "is there a receiver" flag to keep in step. A line goes to exactly one place.
- The forwarder **declines** (the line goes to logcat, nothing is lost) when:
  1. the thread is not known to the JVM. A log line never attaches a thread: attach failure is `FATAL` (ADR-0003), so a
     log line could kill the process. Threads whose logs matter call `jnirt::env()` themselves;
  2. a Java exception is pending (no Java calls are allowed);
  3. a line is written while one is being forwarded (the receiver logs into native, or the forwarder reports a failure);
  4. the receiver threw: the exception is cleared, a warning says so, the original line goes to logcat;
  5. the thread is inside `withCritical` (ADR-0008 §5): between `GetPrimitiveArrayCritical` and its release no JNI call
     is allowed, so the line goes to logcat. A thread-local depth counter, set by `withCritical`, says so.
- `JNIRT_ASSERT` / `JNIRT_FATAL` write straight to logcat and never use the forwarder: after a broken invariant the heap and
  the JVM can not be trusted, and the forwarder calls Java.
- `NativeLogBridge.setNativeLevel(priority)` is a native method that `init()` registers itself; it sets the one level of
  `support/logging`. A priority outside 2..7 is an `IllegalArgumentException`.

### 8. Two copies of jnirt in one process

The log of the first device run showed `startup done` twice: two `.so` each had its own static copy of jnirt and its own
`JNI_OnLoad`. That worked until now. With P4 each copy registers `NativeLogBridge.setNativeLevel` and installs its own
forwarder, so the level set from Kotlin reaches only the copy that registered last. The rule of
`build-and-delivery.md` (one `.so`, one `JNI_OnLoad`, every `register_xxx` in one `init({...})`) is now required, not
advised.

## Consequences

- A native function has no `try` and no `ExceptionCheck`; programmer errors, recoverable errors and Java exceptions all
  become the right Java exception with one line of code at each place.
- A Java exception that goes through native code arrives in Kotlin as the same object.
- Native logs reach Timber and `FileTree` through one receiver; with no receiver nothing changes for the app.
- `jnirt::init` has two more reasons for `JNI_ERR`: a clash of domains, and a missing Kotlin class of the runtime.

## Not in this step

| What                                               | Why / when                                                                                       |
|----------------------------------------------------|--------------------------------------------------------------------------------------------------|
| native backtrace when `entry()` catches an error    | the stack is already unwound at `catch`; capture it at `throw`. Roadmap P8 (`file:line` covers most of it) |
| numbers of the Kotlin enum checked against C++      | needs Kotlin reflection; candidate for the selfcheck                                             |
| `native_raw<&fn>` (no wrapper)                      | no hot path needs it; a `try` that does not throw costs nothing                                  |
| `minSdk` 24 in `android/build.gradle.kts` vs 26     | roadmap P0 says 26; not a P4 matter, to be aligned                                               |

## Tests

| What                                                                                          | Where                                           |
|-----------------------------------------------------------------------------------------------|-------------------------------------------------|
| every row of the table in §2, cause chains (including a Java cause, the cut at 8), the same object, pending wins, logging, references balanced, static / object-returning / `noexcept` natives | `entry_test`            |
| UTF-8 to UTF-16: valid text and every kind of bad sequence                                    | `entry_test`                                    |
| forwarder: sees lines past the level, take / decline, no forwarder, `FATAL` never forwards    | `support_logging_test`                          |
| domains recorded at load                                                                      | `support_error_test`                            |
| domain check: clash, `"jnirt"`, empty, same enum twice, this program's own list              | `domains_test`                                  |
| lines reach Kotlin once; levels; UTF-16; unattached thread; pending; recursion; receiver throws; `setNativeLevel` | `log_bridge_test`        |
| `init` installs the forwarder and registers `setNativeLevel`                                  | `init_test`                                     |
| `nativeCall`, `asError`, `JnirtError`, `NativeException` (JVM unit tests)                     | `android/src/test/.../NativeCallTest.kt`        |
