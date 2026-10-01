# jni-runtime Roadmap

## Status legend

`[x]` done · `[~]` in progress / draft · `[ ]` not started

## Overview

```
P0 Design ─► P1 Build skeleton ─► P2 support (log ✔, error ✔, assert ✔)
                                        │
                                        ▼
                               P3 Runtime foundation
                          (vm/env, refs, signatures, cache, registry)
                                        │
                                        ▼
                           P4 Exceptions + entry() + Java log sink
                                        │
                        ┌───────────────┴───────────────┐
                        ▼                               ▼
                 P5 Type conversion             P6 Peer model
                        │                               │
                        └───────────────┬───────────────┘
                                        ▼
                               P7 Callbacks & subscriptions
                                        ▼
                               P8 Release hardening (R8, sanitizers)
                                        ▼
                               P9 First real consumer (wifi-cameras)
                                        ▼
                     P10 Distribution workflow      P11 Codegen (optional)
```

## Milestones

| Milestone               | Phases  | What you can do afterwards                                                                          |
|-------------------------|---------|-----------------------------------------------------------------------------------------------------|
| **M0** Design frozen    | P0      | Every later API decision can be checked against written invariants                                  |
| **M1** Walking skeleton | P1–P4   | A Kotlin class calls native, gets a value or a typed exception back, and logs reach Timber/FileTree |
| **M2** Safe boundary    | P5      | Strings, arrays and buffers cross safely with compile-time signatures                               |
| **M3** Lifetime model   | P6–P7   | Peers, `close()`, subscriptions and callbacks behave correctly under races                          |
| **M4** Release-ready    | P8      | The same tests pass on a minified R8 build, with sanitizers clean                                   |
| **M5** First consumer   | P9      | `wifi-cameras` runs on the runtime                                                                  |
| **M6** Automation       | P10–P11 | Adding a library is a checklist; boilerplate is generated                                           |

---

## P0. Design and decisions

| Item                                                                  | Status                      |
|-----------------------------------------------------------------------|-----------------------------|
| Architecture overview (repos, layers, rules)                          | `[x]`                       |
| ADR-0001 invariants with glossary (v2)                                | `[~]`                       |
| Toolchain: C++17, NDK r27, runtime min API 26, exceptions enabled     | `[x]`                       |
| Kotlin result type: shape and how public APIs wrap `NativeException`  | `[ ]` waiting for the class |
| Attach policy: daemon vs regular attach, behavior when attach fails   | `[x]` ADR-0003              |
| `GlobalRef` behavior at process shutdown (leak on purpose vs release) | `[x]` ADR-0004              |
| `JNI_OnLoad` failure policy (`JNI_ERR` plus what is logged)           | `[ ]`                       |

**Exit criteria:** ADR-0001 moves from Draft to Accepted, open questions closed.

## P1. Build skeleton

- CMake targets `jnirt::support`, `jnirt::runtime`; common flags via `INTERFACE` (`-fvisibility=hidden`, warnings,
  `-Werror`).
- Host build (gtest) plus Android build via the integration module.
- `if(NOT TARGET jnirt::runtime)` guard for standalone library builds.
- CI check: `core/` never includes `jni.h`.
- Sanitizer options (ASan/UBSan/TSan on host, HWASan on device).
- 16 KB page-size alignment verified for r27 output.

**Exit criteria:** one command builds and tests on host; Android build works through the integration module; both CI
checks run.

## P2. `support/`: no `jni.h`

| Item                                                                    | Status |
|-------------------------------------------------------------------------|--------|
| `log`: levels, filtering, macros, stack buffer, sinks                   | `[x]`  |
| `error`: `jnirt::Error` (domain + code bound to an enum), `IllegalArgument`/`IllegalState` | `[x]`  |
| `assert`: `JNIRT_REQUIRE` / `CHECK` (throw), `ASSERT` (debug crash), `FATAL` (crash) | `[x]`  |

**Exit criteria:** host tests cover all three; `core/` can depend on `support` only.

## P3. Runtime foundation

Build order inside the phase matters:

1. `[x]` **`vm` / `env`** ([ADR-0003](adr/0003-vm-and-env.md)): cache `JavaVM*`, `env()` (`GetEnv` first), `pthread_key`
   detach, `detachCurrentThread()`, thread names, attach failure is `FATAL`.
2. `[x]` **`ref`** ([ADR-0004](adr/0004-references.md)): move-only `LocalRef`, `GlobalRef`, `WeakGlobalRef`; weak refs
   promote to a `LocalRef` or empty; debug checks for detach with live `LocalRef` and cross-thread `LocalRef`.
3. `[x]` **`signature`** ([ADR-0005](adr/0005-signatures.md)): `descriptor<T>` / `signature<F>` built at compile time from
   JNI types and `JavaClass` descriptions; `jobject` and `bool` do not compile.
4. **Minimal exception check**: pending exception after `FindClass` / `GetMethodID`: log and clear.
5. **`class_cache`**: declarative class/method/field descriptions, resolved in `JNI_OnLoad`, stored as global refs.
6. **`registry` + `init()` + selfcheck**: `register_xxx(Registry&)`, `RegisterNatives`, native/Kotlin version match.
   Kotlin: `NativeLoader`.

**Testing:** host gtest fixture that starts a real JVM (`JNI_CreateJavaVM`, `-Xcheck:jni`).

**Exit criteria:**

- a thread attached by someone else stays attached after our call;
- a thread attached by us is detached at exit;
- no unpaired `NewGlobalRef` / `DeleteGlobalRef`;
- a missing class, method or field at startup gives `JNI_ERR` and a log with the exact name.

## P4. Exceptions, `entry()`, Java log sink

- `exception`: C++ error to Java exception mapping, `NativeException` hierarchy with error code, `cause` chain
  preserved.
- `entry()`: single wrapper for every native entry point, catches everything, maps to Java exceptions.
- Pending-exception discipline (check, clear, or return).
- Programmer-error path: `IllegalArgumentException` / `IllegalStateException`; debug builds log and abort.
- `JavaForwardSink` plus Kotlin `NativeLogBridge` (receiver set to Timber by the app), `setNativeLevel` from Kotlin;
  logcat fallback; skip Java when an exception is pending or the sink is recursing; UTF-8 to UTF-16 conversion for
  messages.

**Exit criteria:** every native entry goes through `entry()`; a thrown C++ exception never reaches the JVM as a crash;
Java sink produces no duplicates in logcat.

> **Walking skeleton (end of P4, M1):** one trivial feature end to end (Kotlin class → native call → value or typed
> exception → log in Timber). It is the first real test of the design, so do it here and not at P9.

## P5. Type conversion

- Strings: UTF-16 both ways, invalid sequences handled.
- Primitives, boxed types, `Optional`-like handling of nulls.
- Arrays: copy vs pinning, `Critical` restrictions; direct `ByteBuffer` for large data.
- Collections: `List`, `Map`; automatic type-to-type conversion driven by the signature layer.
- Kotlin specifics: value/inline class name mangling, `@JvmStatic` and companion shape, `suspend` / `Flow` mapped
  explicitly to callbacks.
- Rule: coarse-grained calls; measure crossing cost on Edge II hardware.

**Exit criteria:** round-trip tests for each type; local ref count stays bounded in loops.

## P6. Peer model

- Kotlin `NativePeer : Closeable`, opaque `Long` handle, handle registry of `shared_ptr`.
- In-flight reference counting; `close()` idempotent; use-after-close throws `IllegalStateException`.
- Safety net behind an interface: `Cleaner` on API 33+, `PhantomReference` + `ReferenceQueue` fallback for API 26; leak
  warning in debug.
- Race tests: `close()` concurrent with in-flight calls, double `close()`, use after close.

**Exit criteria:** no crash under stress test (TSan clean); destruction happens exactly once, after the last in-flight
call.

## P7. Callbacks and subscriptions

- `Subscription : Closeable`; closing the owner closes its subscriptions.
- Weak global ref to the Java listener; liveness check before each call.
- Dispatch over a snapshot; safe unsubscribe during dispatch; no calls while holding locks.
- `Looper` / `Executor` captured **at subscription time**; threading contract documented per callback in KDoc.
- `ExceptionCheck` after every call into Java; a throwing listener does not break other listeners.
- Java callback to C++ callback conversion helpers, and the reverse.

**Exit criteria:** reentrancy tests (subscribe/unsubscribe inside callbacks), owner destroyed during dispatch, no
deadlock under stress.

## P8. Release hardening

- `@NativeAccessed` marker annotation, `consumer-rules.pro` in `jnirt-kotlin` and in each library.
- Instrumented tests on a **minified R8** build; selfcheck catches missing keeps at startup.
- CheckJNI, ASan/HWASan runs, TSan on host.
- Symbol handling: keep debug symbols in CI artifacts for tombstone symbolication.
- Verify: only `JNI_OnLoad` exported, one `c++_shared`, 16 KB alignment.

**Exit criteria:** the P3–P7 test suites pass on the minified build.

## P9. First real consumer: `wifi-cameras`

- `register_wificameras(Registry&)`, pure C++ `core/`, `bindings/`, Kotlin API and `consumer-rules.pro`.
- Integration into `unified` as a submodule, per the architecture doc.
- Feed every friction point back into the runtime API before it is frozen.
- Redact credentials in URLs before logging (`redact_url`).

**Exit criteria:** the library runs on Edge II hardware; no API change is needed for the second library.

## P10. Distribution workflow

- Semver tags (`v0.x` until the API stabilizes), submodule bump procedure, CI matrix for `unified` (all modules plus
  minified tests).
- `docs/integration.md`, checklist for adding a new library.
- Deferred until there is a real need: Prefab AAR, per-library `.so`, Maven publishing of native parts.

## P11. Code generation (optional, last)

- Single annotated declaration (KSP) generating both the Kotlin class and the C++ binding.
- `javac -h` stays out: it drives name-based lookup, conflicts with `RegisterNatives`, and does not work for Kotlin.
- Start only after at least two real libraries expose the boilerplate worth generating.

---

## Traceability: original checklist to phases

| Original item                    | Phase                     |
|----------------------------------|---------------------------|
| 1. Type conversion               | P3 (signatures), P5       |
| 2. Callbacks, ownership, cleanup | P6, P7                    |
| 3. Memory: RAII, refs, leaks     | P3 (refs), P6             |
| 4. Error handling                | P2 (error), P4            |
| 5. Logging                       | P2 (done), P4 (Java sink) |
| 6. ProGuard / R8                 | P8                        |
| 7. CMake across repos            | P1, P10                   |
| 8. Code generation               | P11                       |

## Deferred / decided against

Async logger, file logging in native code (handled by Kotlin `FileTree`), Prefab and separate `.so` per library (until a
library is built by someone else), `-fno-exceptions`, `javac -h`.

## Definition of done for every phase

1. Host tests pass (and device tests where applicable).
2. Sanitizer run is clean.
3. `core/` still has no `jni.h`.
4. New behavior is reflected in ADR-0001 or a new ADR.
5. At least one real usage in the reference project.
