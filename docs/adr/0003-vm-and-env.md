# ADR-0003 - JavaVM and JNIEnv

## Status

Proposed. Implementation: roadmap P3.1. Walkthrough with examples: [learning/vm-env-by-example.md](../learning/vm-env-by-example.md).

## Context

ADR-0001 §3 fixes the threading rules: only `JavaVM*` is cached, `JNIEnv*` is never stored or shared,
`current_env()` calls `GetEnv` first, and the runtime detaches only threads it attached itself, through a
`pthread_key` destructor. Roadmap P0 leaves two questions open: daemon vs regular attach, and what happens when
attach fails.

Native code gets a `JNIEnv*` in two ways:

1. The call came from Java/Kotlin. The JVM passes `env` as a parameter; the thread is already attached.
2. The code runs on a native thread (`std::thread`, a thread pool, a callback thread of a third-party SDK). The
   thread is not known to the JVM and has to be attached before any JNI call and detached before it exits.

This ADR defines how the runtime handles case 2, so that code above it does not have to care which thread it is on.

## Decision

### 1. API

```cpp
// <jnirt/runtime/vm.h>
namespace jnirt {
    void setVm(JavaVM* vm);      // called by jnirt::init() from JNI_OnLoad
    JavaVM* vm();
}

// <jnirt/runtime/env.h>
namespace jnirt {
    JNIEnv* env();               // env of the current thread, attaching it if needed; never nullptr
    void detachCurrentThread();  // early detach of a thread that jnirt attached
}
```

There is nothing else: no attach guard class, no options. Names are camelCase, as in `support`.

When a function receives `env` as a parameter, it uses that. `jnirt::env()` is for places where `env` is not
available: native threads, `GlobalRef` destructors, callbacks.

### 2. `setVm` / `vm`

- `JavaVM*` is stored in one `std::atomic<JavaVM*>`. It is never reset: on Android the library is never unloaded,
  so there is no `JNI_OnUnload` and no teardown.
- `setVm` with the same `vm` again is allowed (several `.so` in one process, see §7). A different `vm` is
  `JNIRT_FATAL`.
- `vm()` before `setVm()` is `JNIRT_FATAL`: it is an integration bug (`jnirt::init` missing in `JNI_OnLoad`).

### 3. `env()`

```
GetEnv(JNI_VERSION_1_6)
  JNI_OK        -> return env         (Java thread, or already attached by anyone)
  JNI_EDETACHED -> attach, mark the thread as attached by jnirt in a pthread_key, return env
  JNI_EVERSION  -> JNIRT_FATAL
```

- `JNIEnv*` is not cached, not even in `thread_local`: `GetEnv` is a TLS read in ART.
- **Regular attach** (`AttachCurrentThread`), not daemon. On Android the difference is irrelevant (the VM is never
  shut down, the process is killed). On a host JVM a daemon thread lets the VM exit under a running native thread.
- **Thread name**: taken from `pthread_getname_np` and passed in `JavaVMAttachArgs`, so the thread shows up as
  `camera-rx` in ANR traces and the profiler rather than `Thread-12`. There is no separate API for the name.
- **Attach failure** is `JNIRT_FATAL`. It only happens when the VM is out of memory or dying; the caller cannot
  react.

### 4. Detach

- **Automatic.** The `pthread_key` destructor runs when any pthread exits with a non-null value in the key, and
  calls `DetachCurrentThread`. The key is set only by our own attach, so:
  - a thread attached by someone else (Java thread, other native code) is never touched (ADR-0001 §3.2);
  - a thread attached by us is detached at exit (§3.3), including SDK threads whose loop we do not own, where no
    guard object could be placed.
- **Manual**, `detachCurrentThread()`:

  | Current thread                 | Result                                                                      |
  |--------------------------------|-----------------------------------------------------------------------------|
  | attached by jnirt              | `DetachCurrentThread`, key cleared (no second detach at exit)               |
  | not attached                   | nothing                                                                      |
  | attached by someone else       | `JNIRT_CHECK` fails (`IllegalState`): detaching a thread with Java frames kills the VM |

  It is called as the last statement of a thread function, after all `LocalRef` scopes are closed. A later
  `env()` on the same thread attaches it again. Forgetting it is harmless: the automatic detach still runs.
- Long-lived threads (pools, SDK threads) stay attached for their whole life. This is cheap and expected.

### 5. Misuse is reported, not documented

Every misuse the runtime can detect fails with a message in three parts, in the spirit of `StrictMode`:

```
jnirt: detachCurrentThread() called with 1 LocalRef still alive on thread "worker-3".
  why: local references become invalid after detach; their destructors would use a dead JNIEnv.
  fix: let LocalRef go out of scope before detaching (close the block first).
```

This is only the text passed to `JNIRT_FATAL` / `JNIRT_CHECK`; there is no new mechanism.

| Misuse                                  | Caught by                                   | When         |
|-----------------------------------------|---------------------------------------------|--------------|
| `vm()` / `env()` before `jnirt::init`    | runtime, `FATAL`                            | always       |
| `setVm` with a different `vm`           | runtime, `FATAL`                            | always       |
| `detachCurrentThread()` on a foreign thread | runtime, `CHECK`                        | always       |
| detach with live `LocalRef`             | runtime, per-thread `LocalRef` counter (P3.2) | debug      |
| class lookup on a thread jnirt attached | `jnirt::findClass` (P3.5)                   | debug        |
| raw `FindClass` / `GetMethodID` in library code | CI check                            | CI           |
| `JNIEnv*` used on another thread        | CheckJNI (`-Xcheck:jni`, `debug.checkjni`)  | tests, debug devices |

CheckJNI messages are listed in a troubleshooting doc together with our "fix" line. `JNIEnv*` is not wrapped in
our own type only to detect cross-thread use: CheckJNI already does it.

### 6. Place in `JNI_OnLoad`

`jnirt::init(vm, ...)` calls `setVm(vm)` first and returns `JNI_VERSION_1_6`. Registration, `class_cache` and the
self-check are added inside the same `init` in P3.5 and P3.6.

### 7. Several `.so` in one process

The rule stays "one `.so`, one `JNI_OnLoad`" (build-and-delivery). If a second `.so` ever links `jnirt::runtime`
statically, each copy has its own globals, and vm/env still works: there is one `JavaVM` per process, and whether a
thread is attached is decided by `GetEnv`, not by our state, so one copy never detaches a thread the other
attached. The requirement is that `setVm` accepts the same `vm` twice (§2).

What breaks is outside vm/env: with `-fvisibility=hidden` `jnirt::Error` is a different type in each `.so`, the log
level and class cache are per copy, and `c++_shared` becomes mandatory. The choice between static copies and a
shared `libjnirt.so` is a separate ADR, taken when a second `.so` appears.

## Consequences

- Library code calls `jnirt::env()` on any thread and never manages attach/detach, except for an optional early
  `detachCurrentThread()`.
- Roadmap P0 "attach policy" and "attach failure" are closed by §3.
- `jnirt_runtime` becomes a `STATIC` target with `src/vm.cpp` and `src/env.cpp`. On the host it needs the JDK
  (`find_package(JNI)`); tests also link `libjvm`.

### Tests (host, real JVM)

One JVM per test binary (gtest `Environment`, `JNI_CreateJavaVM` with `-Xcheck:jni`), never destroyed: HotSpot
cannot create a second VM in the same process.

| Check                                   | How                                                                            |
|-----------------------------------------|--------------------------------------------------------------------------------|
| a foreign attach is left alone          | attach manually, call `env()`, the thread is still attached, detach manually   |
| our attach is detached at thread exit   | inside `std::thread` keep `Thread.currentThread()` as a global ref; after `join`, `isAlive()` is false |
| the thread name reaches Java            | `pthread_setname_np("jnirt-test")`, `env()`, `Thread.getName()` matches        |
| repeated `env()` on one thread          | same `JNIEnv*`, no second attach                                               |
| manual detach                           | ours: detached, no double detach at exit; not attached: no-op; foreign: `IllegalState` |
| `vm()` before `setVm()`                 | death test with the expected message                                          |

## Open

- Verify on a device that the `pthread_key` detach does not race with ART's own thread teardown on API 26.
