# Exceptions, `entry()` and logs by example

A learning guide to [ADR-0007](../adr/0007-exceptions-entry-and-log-sink.md). What happens to an error thrown in C++
when Kotlin called that C++, how a library turns it into its own `enum class`, and how a `LOGI` line in C++ ends up in
Timber.

Everything here exists and runs: the complete program is `examples/p4_demo` (camera library), its real output is
`expected-output.txt`. The lines below marked `// output:` are copied from it.

## The rule in one picture

A C++ exception must never cross the JNI boundary: the JVM does not know what to do with it and the process dies. So at
the border every native function sits inside a `try/catch` that turns whatever was thrown into a Java exception.

```
Kotlin                      C++
------                      ---------------------------------------------------
camera.open(url)  ───────►  [ try {                                  ]   <- jnirt writes this, not you
                            [   your function open(env, thiz, url)   ]
                            [ } catch (...) { throw Java exception } ]
Kotlin gets an exception ◄──
```

Kotlin analogy: a `try/catch` that the compiler adds around the body of every `external fun`, with a fixed table of what
becomes what. You never write it and you cannot forget it: `native<&fn>("name")` registers the wrapped function, and
`native<>` is the only way to register one.

## 1. The three kinds of failure, and what Kotlin sees

Pick by asking who is at fault ([error-handling.md](../error-handling.md)). Here is what each one looks like on the
Kotlin side. The native functions are the ones from the demo.

```cpp
jboolean open(JNIEnv* env, jobject thiz, jstring url) {
    JNIRT_REQUIRE(url != nullptr, "url is null");
    std::string address = text(env, url);
    JNIRT_REQUIRE(address.rfind("rtsp://", 0) == 0, "url must start with rtsp://, got %s", address.c_str());

    if (address == "rtsp://locked")
        throw jnirt::Error(wificameras::Errc::AuthFailed, "camera %s rejected login", address.c_str());
    ...
    return JNI_TRUE;
}
```

| In C++                                               | Kotlin receives                                                      | Like in Kotlin            |
|------------------------------------------------------|----------------------------------------------------------------------|---------------------------|
| `JNIRT_REQUIRE(...)` fails                           | `IllegalArgumentException("... (camera_demo.cpp:46)")`               | `require(...)`            |
| `JNIRT_CHECK(...)` fails                             | `IllegalStateException("... (camera_demo.cpp:59)")`                  | `check(...)`              |
| `throw jnirt::Error(Errc::AuthFailed, "...")`        | `NativeException(domain = "wificameras", code = 1, message = "...")` | a library's own exception |
| `std::bad_alloc`                                     | `OutOfMemoryError`                                                   |                           |
| `std::runtime_error("the SDK blew up")` or any other | `NativeException("jnirt", 4, "std::runtime_error: the SDK blew up")` | a bug in a dependency     |
| `throw 42`                                           | `NativeException("jnirt", 4, "unknown C++ exception")`               |                           |
| `JNIRT_ASSERT` / `JNIRT_FATAL`                       | not an exception: the process aborts and the tombstone has the text  | `error("...")` that kills |

```
// output (the demo calls open("http://wrong")):
java.lang.IllegalArgumentException: url must start with rtsp://, got http://wrong (camera_demo.cpp:46)
```

- The `(file:line)` of a programmer error is the C++ place of the failed `REQUIRE`. A C++ stack is not part of a Java
  stack trace, so this is the only pointer to it. Only the file name is added, never the directory.
- Programmer errors are bugs to fix, not situations to handle: do not catch them in Kotlin.
- A recoverable error is a value of **your** `enum class` plus a number that Kotlin knows. That is section 3.

### What if Java has already thrown?

If native code calls Kotlin and Kotlin throws, the call from `class_cache` (`CameraJ::explode(env, thiz)`) throws
`jnirt::JavaException` in C++, and the Java exception is cleared at that moment (it is kept inside). When nobody in C++
catches it, the wrapper throws the **same** Java object again:

```cpp
void callExplode(JNIEnv* env, jobject thiz) { CameraJ::explode(env, thiz); }   // no try, no catch
// Kotlin: java.lang.IllegalStateException: no surface   <- exactly what explode() threw, same stack trace
```

Native code in between is invisible, like a function without a `try` in Kotlin: the exception just passes.

## 2. A cause chain

A real error often has a reason underneath ("could not connect" because "timeout"). In C++ that is
`std::throw_with_nested` inside a `catch`:

```cpp
void streamThenExplode(JNIEnv* env, jobject thiz) {
    try {
        CameraJ::explode(env, thiz);                   // Java throws
    } catch (const JavaException&) {
        std::throw_with_nested(jnirt::Error(wificameras::Errc::StreamLost, "stream lost"));
    }
}
```

Kotlin analogy: `throw StreamLostException("stream lost", cause = e)`. The chain arrives as `Throwable.cause`, from the
top
down, with at most 8 exceptions (a longer chain is cut, which is not an error). A Java exception inside the chain
arrives as the original object, with its own causes.

## 3. The public API of a library: `Outcome`, not exceptions

The application should not have to know that errors come from C++. A library writes two small things in Kotlin:

```kotlin
// 1. its errors, connected to the C++ domain
enum class CameraError(val code: Int, override val kind: ErrorKind) : LibraryError {
    AuthFailed(1, ErrorKind.NeedsUser),
    StreamLost(2, ErrorKind.Transient),
    Timeout(3, ErrorKind.Transient),          // a common error of the runtime (jnirt::Errc::Timeout)
    Unknown(-1, ErrorKind.Fatal);

    companion object : ErrorDomain<CameraError> {
        override val domain = "wificameras"                                  // the string of JNIRT_ERROR_DOMAIN
        override fun fromCode(code: Int) = entries.firstOrNull { it.code == code } ?: Unknown
        override fun fromRuntime(error: JnirtError) = if (error == JnirtError.Timeout) Timeout else Unknown
    }
}

// 2. the public function: a value or a CameraError, never a NativeException
fun open(url: String): Outcome<Boolean, CameraError> = nativeCall(CameraError) { camera.open(url) }
```

The application:

```kotlin
when (val r = api.open("rtsp://locked")) {
    is Outcome.Success -> show(r.value)
    is Outcome.Failure -> when (r.error.kind) {            // by default it branches on the kind, not on every code
        ErrorKind.Transient -> retryLater()
        ErrorKind.NeedsUser -> askForPassword()
        ErrorKind.Fatal -> reportBug()
    }
}
```

```
// output:
open(rtsp://locked) = Failure(AuthFailed, kind = NeedsUser)
grab(50) = Failure(Timeout, kind = Transient)          <- a common error of the runtime, mapped by fromRuntime
streamThenExplode() = Failure(StreamLost, kind = Transient)
```

What `nativeCall` does with an exception from the block:

| It is                                           | `nativeCall` does                             |
|-------------------------------------------------|-----------------------------------------------|
| `NativeException` of this library's domain      | `Failure(CameraError)` through `fromCode`     |
| `NativeException` with domain `"jnirt"`         | `Failure(...)` through `fromRuntime`          |
| `NativeException` of another library            | throws it again, unchanged (not yours to map) |
| anything else (`IllegalArgumentException`, ...) | passes through: it is a bug, not a result     |

There is no class hierarchy under `NativeException` on purpose: the domain and the number say which error it is, and
each
library maps them **once**, here, into its own `enum class`. Adding an error is a new line in the C++ enum and in the
Kotlin
enum with the next number. The numbers are forever: never reuse or renumber.

### Names

The prefix says where an error came from: `Java*` came from Java (`JavaException`), `Native*` came from native code
(`NativeException`), a bare `Error` exists in C++ only (in Kotlin `Error` is `java.lang.Error`). `JnirtError` is the
Kotlin side of the C++ `jnirt::Errc`. `Outcome` is not `Result` so that it does not clash with `kotlin.Result`.

## 4. One domain name, one enum

`JNIRT_ERROR_DOMAIN(wificameras::Errc, "wificameras")` is written once per enum, at global scope. Besides binding the
enum
to the name it records the pair when the library is loaded (a static initialised at load, like the members of a
`JavaClass`). `jnirt::init()` then checks the list and fails with `JNI_ERR` and a line in the log for each of:

- two different enums with the same name: Kotlin receives only the string and the number and could not tell them apart;
- the name `"jnirt"` for anything except `jnirt::Errc`;
- an empty name.

You add nothing to a register function, so there is nothing to forget. The same enum recorded twice is fine. The numbers
of
the Kotlin enum are not checked against C++ (that needs Kotlin reflection; a candidate for the selfcheck).

## 5. Logs from C++ in Timber

Before P4 a `LOGI` went to logcat only, so Timber and the app's `FileTree` never saw it. Now:

```
LOGI("camera opened") -> format -> NativeLogBridge.log(priority, tag, message)   (Kotlin)
                                      │
                         receiver set? ── yes ──► receiver (the app sets it to Timber)
                                      └─ no ───► android.util.Log
```

```kotlin
NativeLogBridge.receiver = NativeLogBridge.Receiver { priority, tag, message ->
    Timber.tag(tag).log(priority, message)
}
```

```
// output:
[android.util.Log] 4/camera: camera opened: rtsp://front    (the receiver here was Log.println(priority, tag, message))
```

- Native **always** calls Kotlin and Kotlin decides, so there is no flag in C++ to keep in step with the receiver. A
  line
  goes to exactly one place, so there are no duplicates in logcat.
- The priority numbers are those of `android.util.Log` (2 = verbose ... 6 = error).
- The level is one for both sides: `NativeLogBridge.setNativeLevel(Log.WARN)` sets the level of the C++ logger, and
  lines
  below it are not even formatted.

### When a line does not go to Kotlin

The forwarder gives up and the line goes to logcat as before, so nothing is lost, when:

1. **the thread is unknown to the JVM.** A log line never attaches a thread: attaching can fail, and failing to attach
   is
   `FATAL`, so a log line could kill the process. If the logs of a thread matter, call `jnirt::env()` on it once.
2. **a Java exception is pending:** nothing may be called in Java while one is.
3. **a line is written while another is being forwarded** (the receiver logs back into native, or reporting a failure
   logs): no recursion.
4. **the receiver threw:** the exception is cleared, one warning says so, the line goes to logcat.

`JNIRT_ASSERT` and `JNIRT_FATAL` never use the forwarder, they write straight to logcat: after a broken invariant
nothing
that calls Java can be trusted, and the tombstone must come out.

## 6. One `.so`

Each `.so` that links jnirt statically has its own copy of the logger, of the forwarder and of `JNI_OnLoad`. With two
copies in one process, the level set from Kotlin reaches only the copy that registered `setNativeLevel` last. So there
is exactly one `.so` and one `JNI_OnLoad`, and every library goes into one `init`:

```cpp
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    return jnirt::init(vm, {registerCamera, registerOtherLibrary});
}
```

## Common mistakes

| Mistake                                                             | What happens                                                              |
|---------------------------------------------------------------------|---------------------------------------------------------------------------|
| Catching `IllegalArgument` in C++ "to be safe"                      | hides a bug of the caller; let it reach Kotlin                            |
| `JNIRT_REQUIRE` in a destructor or a `noexcept` function            | the exception cannot leave it, the process terminates                     |
| Throwing for a normal situation of the outside world with `REQUIRE` | Kotlin gets a bug-class exception; use `jnirt::Error` and an `enum class` |
| Reusing a number of the C++ enum for another meaning                | an old app maps it to the wrong `CameraError`                             |
| Two `.so` with jnirt in one app                                     | the Kotlin log level reaches one of them; see section 6                   |
| Logging from a thread nobody attached and expecting Timber          | the line goes to logcat; call `jnirt::env()` on that thread first         |
