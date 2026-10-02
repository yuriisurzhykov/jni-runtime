# ADR-0001 - Core Invariants of jni-runtime

## Status

Draft

## Glossary

| Term                         | Meaning                                                                                                                                                                                                                                                                |
|------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Invariant**                | A condition that must always hold. Breaking one is a bug in the code, not a runtime failure. This ADR lists the invariants the runtime guarantees and the ones libraries must respect.                                                                                 |
| **Native object**            | A C++ instance that implements the behavior behind a Kotlin class.                                                                                                                                                                                                     |
| **Peer**                     | A Kotlin object together with its native object. The Kotlin object owns the native one. `NativePeer` is the base Kotlin class for peers.                                                                                                                               |
| **Handle**                   | An opaque `Long` stored in the Kotlin peer. It is **not an address**: the runtime resolves it through a registry to a `shared_ptr` to the native object. An unknown or closed handle yields an exception, not a crash.                                                 |
| **In-flight call**           | A native method invocation that has started and not yet returned, possibly on another thread.                                                                                                                                                                          |
| **Closeable**                | The JDK interface `java.io.Closeable` with one method, `close()`. `NativePeer` and `Subscription` implement it. This is where `close()` comes from, and it enables `use { }` in Kotlin. Its contract requires `close()` to be idempotent, which matches invariant 1.3. |
| **Local ref**                | A JNI reference valid only until the native method returns (or until deleted). Limited in number (about 512 on ART by default).                                                                                                                                        |
| **Global ref**               | A JNI reference that keeps a Java object alive until explicitly deleted. Usable from any thread.                                                                                                                                                                       |
| **Weak global ref**          | A global reference that does not keep the object alive; it must be checked for liveness before use.                                                                                                                                                                    |
| **Cleaner**                  | `java.lang.ref.Cleaner` (API 33+). Runs a registered action some time after an object becomes unreachable. Timing is non-deterministic and it may never run before process exit.                                                                                       |
| **NativeAllocationRegistry** | Android helper that ties freeing of native memory to garbage collection of a Java object. Availability depends on API level and must be verified before use.                                                                                                           |
| **finalize()**               | Deprecated `Object` method called by the GC before reclaiming an object. Runs on a finalizer thread with no ordering guarantees, and a watchdog can kill the app if it runs too long. Forbidden.                                                                       |
| **Subscription**             | An object representing one registered callback. Closing it unregisters the callback.                                                                                                                                                                                   |
| **Registry**                 | Runtime component used in `JNI_OnLoad` where libraries register their classes, methods and native functions. Not to be confused with the handle registry.                                                                                                              |
| **entry()**                  | The single wrapper around every native entry point. It catches all C++ exceptions and converts them to Java exceptions.                                                                                                                                                |
| **Tombstone**                | Android's crash dump for native crashes (signal or `abort`), containing the native backtrace.                                                                                                                                                                          |
| **Programmer error**         | A bug in the caller or the library (invalid argument, use after close, broken invariant). Not something the caller is expected to handle.                                                                                                                              |
| **Recoverable error**        | An expected failure in the environment (network, device, timeout) that callers are expected to handle.                                                                                                                                                                 |

## 1. Ownership

- 1.1 A Kotlin peer **owns** its native object. Native code never controls the lifetime of a Java object.
- 1.2 The Kotlin side stores only a **handle**, never a raw pointer.
- 1.3 `close()` (from `Closeable`) is **idempotent**. Any other call on a closed peer throws `IllegalStateException`.
- 1.4 **Explicit `close()` is the primary mechanism.** A safety net for forgotten peers sits behind a Kotlin interface:
  `Cleaner` on API 33+, a `PhantomReference` + `ReferenceQueue` fallback below. The safety net logs a leak warning in
  debug builds. `finalize()` is forbidden.

## 2. close () vs in-flight calls

- 2.1 Every native call on a peer holds a reference to its native object for the duration of the call.
- 2.2 `close()` marks the peer closed and calls the `onClose()` hook of its native object, so the resource is released
  at once (a blocked call wakes up). New calls fail immediately. The native object is destroyed when the last
  in-flight call releases it. *Details in ADR-0009.*
- 2.3 `close()` never blocks waiting for in-flight calls or callbacks.

## 3. Threading

- 3.1 Only `JavaVM*` is cached. `JNIEnv*` is never stored or shared between threads. The one exception is
  `LocalRef`, which keeps the `env` of the thread that owns it for its own scope (ADR-0004 §3).
- 3.2 `jnirt::env()` calls `GetEnv` first. A thread already attached by someone else is left alone.
- 3.3 The runtime detaches only threads it attached itself, through a `pthread_key` destructor.
  *Details in ADR-0003.*
- 3.4 Callbacks are invoked **on the thread where the event occurs**, never while holding a runtime or library lock.
- 3.5 Thread affinity is the **library's** responsibility. Kotlin APIs accept an `Executor` (or a `Handler`) at
  subscription time. The `Looper` is captured **at subscription time**, never inside the callback. Each callback
  documents
  its thread in KDoc. The executor is mandatory, with no default (ADR-0010 §5).

## 4. References

- 4.1 All `jclass`, `jmethodID`, `jfieldID` are resolved once in `JNI_OnLoad` and kept as global refs. `FindClass` is
  never called from native threads. *Details in ADR-0006.*
- 4.2 Ownership of references is expressed with RAII types: `LocalRef`, `GlobalRef`, `WeakGlobalRef`. Raw `jobject` does
  not appear in library APIs. *Details in ADR-0004.*
- 4.3 Long loops and long-running native methods use `PushLocalFrame` or explicit deletion.

## 5. Callbacks and subscriptions

- 5.1 A callback belongs to a **subscription**, not directly to an object.
- 5.2 `subscribe()` returns a `Subscription` (`Closeable`). Closing the owner closes its subscriptions.
- 5.3 Native code holds a **strong global ref** to the Java listener while the subscription is open, and checks the
  subscription's `active` flag before each call. (A weak reference loses lambdas to the garbage collector: ADR-0010 §2.)
- 5.4 Unsubscribing during dispatch is safe: dispatch iterates over a snapshot.
- 5.5 After every call into Java, `ExceptionCheck` is mandatory.

## 6. Errors

- 6.1 **C++ exceptions stay enabled** and are allowed inside `core`. They **never cross the JNI boundary**: `entry()`
  catches everything.
- 6.2 **Programmer error:** `IllegalArgumentException` / `IllegalStateException`. In debug builds, log and `abort`.
  *Superseded by ADR-0002: these are thrown in debug and release alike; only invariant errors crash.*
- 6.3 **Recoverable error:** native throws `jnirt::Error` (a domain and a code); Kotlin receives one class,
  `NativeException(domain, code)`, without subclasses (ADR-0002 §3). Native never constructs Kotlin result objects.
- 6.4 The **public Kotlin API** of a library may wrap these exceptions into the project's result type. One error is
  reported in one way only: never both a result and an exception.
- 6.5 With a pending Java exception, only the minimal allowed set of JNI calls is made: check, clear, or return.
  Deleting references is on that list, so `LocalRef` / `GlobalRef` destructors are safe during unwinding (ADR-0004 §7).
- 6.6 `cause` chains are preserved. The native stack is **not** part of the Java stack trace: it goes to the log, and
  crashes rely on tombstones.

## 7. Types and signatures

- 7.1 Strings cross the boundary as **UTF-16** (`GetStringRegion`), not modified UTF-8. *Details in ADR-0008.*
- 7.2 Signatures are **generated at compile time** from C++ types, never written by hand. *Details in ADR-0005.*
- 7.3 Large data uses direct `ByteBuffer`. Boundary crossings are coarse-grained. *Details in ADR-0008.*
- 7.4 Natives are registered with `RegisterNatives`, not by mangled names.

## 8. Cross-library rule

- 8.1 No native object, handle, or reference crosses a library boundary. Libraries communicate only through Kotlin APIs.

## 9. Startup validation

- 9.1 `JNI_OnLoad` runs a **fail-fast self-check**: every registered class, method and field exists, and native and
  Kotlin
  runtime versions match. *Details in ADR-0006.*
- 9.2 This catches R8/ProGuard breakage at startup, not in production.

## 10. Toolchain

- 10.1 C++17. NDK r27+ (LTS or newer), set explicitly via `ndkVersion`.
- 10.2 Runtime is designed for a minimum API of 24/26, even if the project currently uses 33.
