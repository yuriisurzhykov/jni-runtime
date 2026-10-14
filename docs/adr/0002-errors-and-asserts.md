# ADR-0002 - Errors and asserts

## Status

Accepted. The C++ side (`support`) is implemented; the Kotlin side and `entry()` are implemented in P4 and described in
[ADR-0007](0007-exceptions-entry-and-log-sink.md), which also fixes the names `JnirtError` and `Outcome` used below.

## Context

ADR-0001 §6 fixes the rules: C++ exceptions never cross JNI, `entry()` catches everything, recoverable errors become
`NativeException` with a code, programmer errors become `IllegalArgumentException` / `IllegalStateException`.
`support` has no `jni.h`, so it can only define the C++ side. Turning C++ errors into Java exceptions is `runtime`'s
job.

## Decision

### 1. Three kinds of failure

The kind depends on **who is at fault**.

| Kind                  | Who is at fault        | C++                                         | What the caller gets                                                                                  |
|-----------------------|------------------------|---------------------------------------------|-------------------------------------------------------------------------------------------------------|
| **Programmer error**  | the code that calls us | `JNIRT_REQUIRE` / `JNIRT_CHECK`             | Kotlin: `IllegalArgumentException` / `IllegalStateException`. Pure C++: process terminates, tombstone |
| **Recoverable error** | the outside world      | `throw jnirt::Error(Errc::X, "text")`       | Kotlin: `NativeException(domain, code)` → library's Kotlin error type                                 |
| **Invariant error**   | our own code           | `JNIRT_ASSERT` (debug only) / `JNIRT_FATAL` | Process aborts, message in logcat and tombstone                                                       |

Rule of thumb: a caller's mistake or an environment failure is reported and the caller decides; our own bug is
never reported, it is a crash.

### 2. One API for `core` and `bindings`

The macros and types live in `support` and are the same everywhere. What differs is who catches:

- `REQUIRE` / `CHECK` throw `jnirt::IllegalArgument` / `jnirt::IllegalState`. Called from Kotlin, `entry()` turns them
  into the Java exceptions, and the Java stack points at the Kotlin caller. With no `entry()` above (C++ caller, own
  thread) nobody catches, `std::terminate` runs, and the tombstone shows the message.
- `ASSERT` / `FATAL` are never caught by `entry()`.

This **changes ADR-0001 §6.2** ("in debug builds, log and `abort`"). `REQUIRE` / `CHECK` behave the same in debug and
release, so a test for `IllegalArgumentException` does not kill a debug build. Crashing is kept for invariants only.

Restriction: `REQUIRE` / `CHECK` must not be used in destructors or `noexcept` functions (the exception cannot leave
them, so it becomes a crash).

### 3. Recoverable errors: flat model, domain + code

An error is a **domain** (one per library), a **code** (value of that library's `enum class`) and a message. There is no
class hierarchy: it would need a Java class per code.

```cpp
namespace wificameras { enum class Errc { AuthFailed = 1, StreamLost = 2 }; }
JNIRT_ERROR_DOMAIN(wificameras::Errc, "wificameras")     // once, at global scope

throw jnirt::Error(wificameras::Errc::AuthFailed, "camera %s rejected login", host);
```

- The domain is bound to the enum, so the code and the domain cannot be mixed up (compile-time check).
- Generic codes (`Timeout`, `NotFound`, `Io`, `Internal`) live in domain `"jnirt"`.
- Code numbers are stable forever: Kotlin depends on them. A removed code keeps its number.
- Domain names are unique across libraries: `init` checks it at startup (ADR-0007 §6).
- Cause chains use the standard `std::throw_with_nested`; `entry()` maps them to Java `cause`.
- No `Result<T>` in C++: exceptions are already allowed (ADR-0001 §6.1), and a second way to report the same error
  would only add a choice. It can be added later if a hot path needs it.

### 4. Kotlin side

```kotlin
class NativeException(val domain: String, val code: Int, message: String?, cause: Throwable? = null)

enum class ErrorKind { Transient, NeedsUser, Fatal }          // runtime, common to all libraries

interface ErrorDomain<E> {
    val domain: String;fun fromCode(code: Int): E
}

fun <E> NativeException.asError(d: ErrorDomain<E>): E?        // null if the error is from another domain
```

- The application never sees `NativeException`. Each library's **public API** maps native errors once, into its own
  `enum class` (with an `Unknown` entry and an `ErrorKind` per value) and returns `Outcome<T, LibraryError>`.
  Generic errors (e.g. `Timeout`) are folded into the library's enum.
- Applications branch on `error.kind` by default, and on the specific code only when they need a special reaction.
- Programmer errors stay plain exceptions and are not caught.
- Keep enums small: a code exists only if a caller reacts to it differently.

### 5. What `entry()` does

| Caught                      | Java                                                  |
|-----------------------------|-------------------------------------------------------|
| `JavaException` (see below) | the original `Throwable`, unchanged                   |
| `IllegalArgument`           | `IllegalArgumentException`                            |
| `IllegalState`              | `IllegalStateException`                               |
| `jnirt::Error`              | `NativeException(domain, code, message)`, cause chain |
| `std::bad_alloc`            | `OutOfMemoryError`                                    |
| anything else               | `NativeException("jnirt", Internal, what())` + log    |

Pending Java exceptions: after every call into Java the runtime calls `ExceptionCheck`. If one is pending, it is
moved into a `GlobalRef`, cleared (`ExceptionClear`) and rethrown as a C++ `JavaException`. The C++ stack then unwinds
with no Java exception pending, so RAII destructors may call JNI. `entry()` rethrows the original `Throwable`. If a
Java exception is already pending when `entry()` catches a C++ one, the pending one wins and the C++ one is logged.
Library code never calls `ExceptionCheck` itself.

`entry()` logs what it catches, once. `Error` and the contract exceptions never log by themselves.

### 6. Crash path (`ASSERT`, `FATAL`)

Format the message into a fixed stack buffer (no heap: after corruption or OOM the heap cannot be trusted), log it with
tag `jnirt`, store it with `android_set_abort_message` (it appears as `Abort message:` in the tombstone), then
`abort()`. A per-thread guard sends a failure inside the failure path straight to `abort()`.

## Consequences

- `core` gets errors and asserts without `jni.h`, testable on a host (35 tests, clang and gcc, ASan/UBSan clean).
- `support` builds off-device (stderr sink), and `jnirt::options` enables exceptions and RTTI explicitly.
- `entry()`, pending exceptions and the Kotlin API have a fixed contract (implemented in ADR-0007).

## Open

- Verify on a device: `android_set_abort_message` at the project's minSdk, and the tombstone text for an uncaught
  `IllegalArgument` (libc++abi prints the exception type and `what()`).
- Shape of the Kotlin result type: `Outcome` (ADR-0007 §5), to be tested by the first real library (P9).
- `-Werror` in `jnirt::options` (not enabled yet).
