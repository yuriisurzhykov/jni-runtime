# Errors and asserts

`jnirt` has three kinds of failure. Pick by asking **who is at fault**.

| Who is at fault                     | Kind              | What you write                            |
|-------------------------------------|-------------------|-------------------------------------------|
| The code that calls yours           | Programmer error  | `JNIRT_REQUIRE(...)` / `JNIRT_CHECK(...)` |
| The outside world (network, device) | Recoverable error | `throw jnirt::Error(Errc::X, "text")`     |
| Your own code                       | Invariant error   | `JNIRT_ASSERT(...)` / `JNIRT_FATAL(...)`  |

Everything lives in `support` (no `jni.h`), so it works the same in `core` and in `bindings`.

```cpp
#include <jnirt/support/assert.h>
#include <jnirt/support/error.h>
```

## Programmer errors: `REQUIRE` and `CHECK`

Like Kotlin's `require` and `check`. Same behavior in debug and release.

```cpp
JNIRT_REQUIRE(timeoutMs > 0, "timeoutMs must be positive, got %d", timeoutMs);  // bad argument
JNIRT_CHECK(connected_, "grab() before connect()");                              // wrong state
```

They throw `jnirt::IllegalArgument` / `jnirt::IllegalState`. When the call came from Kotlin, `entry()` turns them into
`IllegalArgumentException` / `IllegalStateException` and the stack trace points at the Kotlin caller. When the call is
pure C++ and nobody catches, the process terminates and the tombstone shows the message.

Do not use them in destructors or `noexcept` functions: the exception cannot leave them, so it becomes a crash.

## Recoverable errors: `jnirt::Error`

An error is a **domain** (one per library), a **code** (a value of the library's `enum class`) and a message.

```cpp
// wificameras/errors.h
namespace wificameras {
    enum class Errc { AuthFailed = 1, StreamLost = 2 };   // the numbers are forever: Kotlin depends on them
}
JNIRT_ERROR_DOMAIN(wificameras::Errc, "wificameras")     // once, at global scope, outside any namespace

// anywhere
throw jnirt::Error(wificameras::Errc::AuthFailed, "camera %s rejected login", host);
```

- The message is `printf`-style and checked by the compiler.
- The domain comes from the enum, so a code cannot be paired with the wrong domain.
- Generic codes `jnirt::Errc::{Timeout, NotFound, Io, Internal}` (domain `"jnirt"`) can be thrown from any library.
- Test in C++ with `if (e.is(Errc::AuthFailed)) ...`.
- To keep the cause, throw inside a `catch`: `std::throw_with_nested(jnirt::Error(...))`.
- Add a new error: add a line to your enum with the next number. Never reuse or renumber. Nothing in the runtime
  changes.

## Invariant errors: `ASSERT` and `FATAL`

```cpp
JNIRT_ASSERT(refCount_ > 0, "refcount underflow");   // debug: crash. release: removed, condition not evaluated
JNIRT_FATAL("unknown state %d", static_cast<int>(s)); // always crash (also for impossible switch branches)
```

The message goes to logcat and to the tombstone (`Abort message:`), then the process aborts. Use them for bugs in our
own code, never for input or environment problems.

## Rules

- A message is mandatory, and its arguments are evaluated only on failure.
- Do not catch `IllegalArgument` / `IllegalState`: they are bugs to fix, not situations to handle.
- Report an error once: either throw, or log and handle. `entry()` logs what it catches.
- Do not put `JNIRT_ERROR_DOMAIN` for the same enum in two places, and keep domain names unique across libraries.

The Kotlin side (`NativeException`, mapping to a library's own error enum) is described in
[ADR-0002](adr/0002-errors-and-asserts.md).
