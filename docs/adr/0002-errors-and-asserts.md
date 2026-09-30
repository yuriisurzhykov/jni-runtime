# ADR-0002 - Errors and asserts in `support`

## Status

Proposed

## Context

ADR-0001 §6 says: C++ exceptions are allowed and never cross JNI; `entry()` catches everything; a caller's mistake
becomes `IllegalArgumentException` / `IllegalStateException`; an expected failure becomes `NativeException` with a code.

`support` has no `jni.h`, so it only defines the C++ side. Turning C++ errors into Java exceptions is `runtime`'s job
(P4, `entry()`).

## Three situations, three tools

| Situation                                       | Kotlin analogy                         | C++ in `support`                   | What Kotlin sees                |
|-------------------------------------------------|----------------------------------------|------------------------------------|---------------------------------|
| Expected failure (timeout, no device, bad data) | `throw IOException(...)`               | `throw jnirt::Error(domain, code, msg)` | `NativeException(domain, code)` |
| Caller passed garbage / called at a wrong time  | `require(...)` / `check(...)`          | `JNIRT_REQUIRE` / `JNIRT_CHECK`    | `IllegalArgumentException` / `IllegalStateException` |
| Our own bug, "this can never happen"            | `assert` / `error("unreachable")`      | `JNIRT_ASSERT` / `JNIRT_FATAL`     | nothing: the process crashes, tombstone |

That is the whole API: three exception classes and four macros.

### 1. Expected failures: `jnirt::Error`

```cpp
namespace wificameras {
enum class Code { AuthFailed = 1, StreamLost = 2 };
constexpr const char* kDomain = "wificameras";
}

throw jnirt::Error(kDomain, Code::AuthFailed, "camera %s rejected credentials", host);
```

- An error is `domain` (a string, one per library) + `code` (a number from the library's own enum) + `message`.
  Kotlin gets the same three fields in `NativeException`.
- The runtime has its own domain `"jnirt"` with a few generic codes (`Timeout`, `NotFound`, `Io`, `Internal`, ...).
- The message is formatted `printf`-style, like `LOGx`.
- No separate `Result<T>` type. ADR-0001 already allows exceptions, so a second way to report the same error would only
  add choice. It can be added later if a real hot path needs it.

### 2. Caller mistakes: `JNIRT_REQUIRE`, `JNIRT_CHECK`

```cpp
JNIRT_REQUIRE(port > 0, "port must be positive, got %d", port);   // -> IllegalArgumentException
JNIRT_CHECK(!closed_, "camera is closed");                         // -> IllegalStateException
```

Exactly like Kotlin's `require` / `check`: if the condition is false, throw. Same behavior in debug and release.

**This changes ADR-0001 §6.2**, which asked for "log and abort in debug builds" here. Reason: with abort, debug and
release behave differently, and a test that checks for `IllegalArgumentException` crashes the debug build. Crashing is
kept for our own bugs only (section 3).

### 3. Our own bugs: `JNIRT_ASSERT`, `JNIRT_FATAL`

```cpp
JNIRT_ASSERT(refCount_ > 0, "refcount underflow");   // debug: crash; release: removed, not even evaluated
JNIRT_FATAL("unknown state %d", static_cast<int>(s)); // always crash
```

- `JNIRT_ASSERT` is for cheap self-checks during development. It disappears from release builds.
- `JNIRT_FATAL` is for places where continuing would corrupt data. Also used for impossible `switch` branches.
- Crash sequence: log `file:line: message` with tag `jnirt` (logcat), put the same text into the tombstone
  (`android_set_abort_message`), then `abort()`.
- "Debug" means the build does not define `NDEBUG` (standard for Android debug builds).

## Where these meet Java (P4, fixed here so P4 needs no redesign)

`entry()` wraps every native method and translates:

| C++                                  | Kotlin                                |
|--------------------------------------|---------------------------------------|
| `jnirt::Error`                       | `NativeException(domain, code, message)` |
| from `JNIRT_REQUIRE`                 | `IllegalArgumentException`            |
| from `JNIRT_CHECK`                   | `IllegalStateException`               |
| Java exception thrown by a callback  | the same Java exception, unchanged    |
| out of memory                        | `OutOfMemoryError`                    |
| anything else                        | `NativeException("jnirt", Internal)` + log |

Java exceptions inside native code: after each call into Java the runtime checks for a pending exception. If there is
one, it is taken out of JNI and carried up as a C++ exception, and `entry()` rethrows the original to Kotlin. Library
code never calls `ExceptionCheck` itself.

## Build and tests

- `support` gets a host (non-Android) logging sink, so it builds and tests on a laptop. Today CMake stops with
  `FATAL_ERROR` outside Android.
- Host tests: each macro and `jnirt::Error`, in debug and release.

## Also in this change

- Namespace `support::` becomes `jnirt::` (logging becomes `jnirt::logging`). `LOGx` macro names stay as they are.
- Remove the duplicated `LOG_DEFINE_TAG` in `macros.h`.

## Files

```
support/include/jnirt/support/error.h    jnirt::Error, runtime domain codes
support/include/jnirt/support/assert.h   JNIRT_REQUIRE, JNIRT_CHECK, JNIRT_ASSERT, JNIRT_FATAL
support/src/error.cpp
support/src/assert.cpp                   crash path
support/src/android/abort_android.cpp    android_set_abort_message
support/src/host/...                     stderr sink and abort for host builds
tests/host/support/...
```
