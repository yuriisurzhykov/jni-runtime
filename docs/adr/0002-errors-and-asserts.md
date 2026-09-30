# ADR-0002 - Errors and asserts in `support`

## Status

Proposed

## Context

ADR-0001 §6 fixes the rules: C++ exceptions are allowed in `core` and never cross JNI; programmer errors become
`IllegalArgumentException` / `IllegalStateException` (debug: log + abort); recoverable errors become `NativeException`
with a code; one error is reported one way only; a pending Java exception allows only check / clear / return.

`support` must not include `jni.h`. So this ADR splits the work in two:

- **`support` (P2)**: pure C++ error model, `Result<T>`, assert macros, the failure handler. Host-testable.
- **`runtime` (P4)**: pending JNI exceptions, mapping to Java classes, `entry()`. Only the contract is fixed here.

Current state: `error.h` and `assert.h` are empty; logging works; the support target builds only for Android
(`FATAL_ERROR` otherwise), which blocks the P2 exit criterion "host tests cover all three".

## Decisions

### D1. Two kinds of failure, two mechanisms

| Kind                                           | Mechanism in C++                                  | In Java                                            |
|------------------------------------------------|---------------------------------------------------|----------------------------------------------------|
| **Recoverable** (network, timeout, not found)  | `Error` value: `Result<T>`, or `jnirt::Exception` | `NativeException(domain, code, message, cause)`    |
| **Programmer error** (bad argument, bad state) | `JNIRT_REQUIRE` / `JNIRT_CHECK` → `ContractViolation` | `IllegalArgumentException` / `IllegalStateException` |
| **Broken internal invariant / corruption**     | `JNIRT_DCHECK`, `JNIRT_UNREACHABLE`, `JNIRT_FATAL` | none: process aborts, tombstone                   |

`Error` never describes a programmer error, and an assert never describes a recoverable one.

### D2. Error model (`jnirt/support/error.h`)

Built on `std::error_code` / `std::error_category`: standard, allocation-free, and each feature library can add its own
domain without touching the runtime.

```cpp
namespace jnirt {

// Generic codes of the runtime domain. Values are stable: they reach Kotlin.
enum class Errc : int32_t {
    Unknown = 1, InvalidData, NotFound, AlreadyExists, Timeout, Cancelled,
    Unavailable, PermissionDenied, Io, Unsupported, Internal,
};
const std::error_category& runtime_category() noexcept;   // name() == "jnirt"
std::error_code make_error_code(Errc) noexcept;          // + is_error_code_enum<Errc> specialization

class Error {
public:
    Error(std::error_code code, std::string message = {});
    Error& caused_by(Error cause);                       // builds the chain

    std::error_code code() const noexcept;
    const char* domain() const noexcept;                 // category name, e.g. "jnirt", "wificameras"
    const std::string& message() const noexcept;
    const Error* cause() const noexcept;                 // nullptr at the end of the chain
    std::string describe() const;                        // "domain:code message <- cause ..." for logs
private:
    std::error_code code_;
    std::string message_;
    std::shared_ptr<const Error> cause_;                 // cheap copy, immutable chain
};

// Thrown when a Result is not convenient (constructors, deep call stacks).
class Exception : public std::exception {
public:
    explicit Exception(Error error);
    const Error& error() const noexcept;
    const char* what() const noexcept override;
};

} // namespace jnirt
```

A library domain looks like this and needs nothing from the runtime:

```cpp
enum class CameraErrc : int32_t { AuthFailed = 1, StreamLost };
const std::error_category& camera_category() noexcept;   // name() == "wificameras"
```

Java side (P4): `NativeException(domain: String, code: Int, message, cause)`. Kotlin compares `(domain, code)`.

### D3. `Result<T>` (`jnirt/support/result.h`)

C++17 has no `std::expected`, so a small own type:

```cpp
template <class T> class Result;          // holds T or Error
template <>        class Result<void>;    // success or Error

Result<Frame> r = decode(buf);
if (!r) return r.error();                 // implicit Error -> Result<U>
use(*r);

JNIRT_TRY(connect(host));                 // Result<void>: return error on failure
JNIRT_TRY_ASSIGN(auto frame, decode(buf));// declares `frame` or returns the error
```

- `value()` / `operator*` on an error is a programmer error: `JNIRT_CHECK` failure, not UB.
- `[[nodiscard]]` on the class: an ignored `Result` is a compile warning (`-Werror` makes it an error).
- `JNIRT_TRY*` are plain macros, no GNU statement-expressions.
- Rule "one way only": a function either returns `Result` or throws `jnirt::Exception`, never both. Bindings convert
  a `Result` with `value_or_throw()` right before returning to `entry()`.

### D4. Asserts (`jnirt/support/assert.h`)

| Macro                               | Debug                   | Release                                 | Use for                                           |
|-------------------------------------|-------------------------|-----------------------------------------|---------------------------------------------------|
| `JNIRT_REQUIRE(cond, fmt, ...)`     | log + abort *           | log + throw `ContractViolation{InvalidArgument}` | arguments that came from a caller (Kotlin)        |
| `JNIRT_CHECK(cond, fmt, ...)`       | log + abort *           | log + throw `ContractViolation{IllegalState}`    | state preconditions (use after close, wrong order) |
| `JNIRT_DCHECK(cond, fmt, ...)`      | log + abort             | compiled out, `cond` not evaluated      | internal invariants, hot paths                    |
| `JNIRT_UNREACHABLE(fmt, ...)`       | log + abort             | log + abort                             | impossible `switch` branches                      |
| `JNIRT_FATAL(fmt, ...)`             | log + abort             | log + abort                             | corruption where continuing is worse than dying   |

`*` "abort in debug" follows ADR-0001 §6.2, but it makes debug tests for `IllegalArgumentException` impossible. So the
behavior is a **runtime policy** with a build-type default:

```cpp
enum class ViolationPolicy : uint8_t { Abort, Throw };
void setViolationPolicy(ViolationPolicy) noexcept;       // default: Abort in debug, Throw in release
```

Tests and QA builds switch to `Throw`. `DCHECK`, `UNREACHABLE` and `FATAL` ignore the policy.

Details:

- Message is `printf`-style with `-Wformat` checks, like `LOGx`. The format and its arguments are evaluated only on
  failure.
- One failure handler, `[[noreturn]]` where it aborts, always compiled (not `inline`), so debug and release TUs do not
  break ODR. It logs `file:line func: CHECK failed: <expr>: <message>` with a fixed tag `jnirt`.
- Before `abort()` the handler calls a platform hook: on Android `android_set_abort_message` (in `liblog`), so the
  message lands in the tombstone. Host: `stderr`.
- Re-entry guard: a failure inside the failure handler goes straight to `abort()`.
- `ContractViolation : std::logic_error` carries the kind (`InvalidArgument` / `IllegalState`) and the formatted
  message.
- Switch: `JNIRT_DCHECK_IS_ON` = `!defined(NDEBUG)` by default; the CMake option `JNIRT_FORCE_DCHECKS=ON` turns DCHECKs
  on in release builds (QA).

### D5. Contract with `runtime` (P4, only fixed here)

`entry()` catch order:

| Caught                                  | Java                                                          |
|-----------------------------------------|---------------------------------------------------------------|
| `JavaException` (see below)             | rethrow the original `Throwable` unchanged                    |
| `ContractViolation{InvalidArgument}`    | `IllegalArgumentException`                                    |
| `ContractViolation{IllegalState}`       | `IllegalStateException`                                       |
| `jnirt::Exception`                      | `NativeException(domain, code, message)`, cause chain rebuilt |
| `std::bad_alloc`                        | `OutOfMemoryError`                                            |
| `std::exception`, `...`                 | `NativeException("jnirt", Internal, what())` + log            |

Pending JNI exceptions:

- After every call into Java, runtime helpers call `ExceptionCheck`. If set, they take the throwable into a `GlobalRef`,
  `ExceptionClear`, and throw C++ `JavaException`. The C++ stack unwinds with no JNI exception pending, so RAII
  destructors (`DeleteLocalRef` etc.) are legal.
- `entry()` rethrows the stored throwable. If C++ wants context, it throws `jnirt::Exception` with the Java throwable as
  `cause` (the Java cause chain is preserved, §6.6).
- Callbacks on native threads have no `entry()` above them: the dispatcher catches `JavaException`, logs it and moves to
  the next listener (P7).
- If a Java exception is already pending when `entry()` catches a C++ one, the pending one wins; the C++ one is only
  logged.
- `JNIRT_DCHECK_NO_PENDING(env)` lives in `runtime`, at the start of every JNI helper.
- An assert failure while a Java exception is pending is fine: the handler only uses `support` logging (logcat), never
  JNI.

### D6. Logging

- Asserts and `Error::describe()` log through `support::logging` with the tag `jnirt`, level `Error`.
- `Error` itself never logs: it is logged once by whoever decides not to propagate it (usually `entry()`), to avoid
  duplicates.

### D7. Build and tests

- `support` gets a host sink (`src/host/logging/log_sink_stderr.cpp`) and a host abort hook; the `FATAL_ERROR` goes.
- Host gtest target `tests/host/support`: `Error` chain and categories, `Result` / `JNIRT_TRY`, every assert in both
  policies (`EXPECT_DEATH` for abort, `EXPECT_THROW` for throw), DCHECK compiled out in release.

## Files

```
support/include/jnirt/support/
  error.h              Errc, runtime_category, Error, Exception
  result.h             Result<T>, Result<void>, JNIRT_TRY, JNIRT_TRY_ASSIGN
  assert.h             JNIRT_REQUIRE/CHECK/DCHECK/UNREACHABLE/FATAL, ContractViolation, ViolationPolicy
support/src/
  error/error.cpp
  assert/failure.cpp            handler, policy, re-entry guard
  platform/abort_hook.h         internal, like log_sink.h
  android/assert/abort_hook_android.cpp
  host/assert/abort_hook_host.cpp
  host/logging/log_sink_stderr.cpp
tests/host/support/...
```

## Open questions

1. Namespace: the existing code uses a global `support::`. This ADR proposes `jnirt::` for the new API and moving
   logging to `jnirt::support::logging` in the same change. Collision risk of a global `support` in consumer code is
   high.
2. `LOGx` macros are unprefixed and clash with the common NDK idiom `#define LOGE(...) __android_log_print(...)` in
   consumer code. Proposal: `JNIRT_LOGx` as the canonical names, `LOGx` kept as an opt-in alias.
3. `-Werror` is in the roadmap but not in `jnirt::options`; `[[nodiscard]]` on `Result` relies on it.

## Consequences

- `core` gets errors, results and asserts without `jni.h`, testable on host.
- P4 has a fixed contract for `entry()` and pending exceptions, so no redesign there.
- Debug tests can check Java exceptions from programmer errors by switching the policy.
