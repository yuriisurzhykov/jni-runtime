# ADR-0004 - JNI references: `LocalRef`, `GlobalRef`, `WeakGlobalRef`

## Status

Proposed. Implementation: roadmap P3.2. Principles and the way of thinking behind it:
[learning/thinking-in-jnirt.md](../learning/thinking-in-jnirt.md).

## Context

Every `jobject` that native code gets from JNI is not the object itself but a **reference** that the JVM treats as a
GC root. ADR-0001 §4.2 requires reference ownership to be expressed with RAII types. Three things go wrong without
them:

- **Local references run out.** The per-thread table is limited (about 512 on ART by default). A loop that calls
  `GetObjectArrayElement` without `DeleteLocalRef` overflows it. On a native thread local references are never freed
  until the thread is detached.
- **Global references leak.** A forgotten `DeleteGlobalRef` keeps a Java object alive forever.
- **Weak references race with the GC.** "Check that it is alive, then use it" leaves a gap in which the object can be
  collected.

Roadmap P0 also leaves open what a `GlobalRef` does at process shutdown.

## Decision

### 1. API

```cpp
// <jnirt/runtime/ref.h>
namespace jnirt {
    template <class T = jobject> class LocalRef;        // T: jobject, jclass, jstring, jobjectArray, ...
    template <class T = jobject> class GlobalRef;
    template <class T = jobject> class WeakGlobalRef;
}
```

| Member                        | `LocalRef` | `GlobalRef` | `WeakGlobalRef` | Meaning                                                    |
|-------------------------------|:----------:|:-----------:|:---------------:|------------------------------------------------------------|
| default constructor           | ✓          | ✓           | ✓               | empty reference                                            |
| `LocalRef(JNIEnv*, T)`        | ✓          |             |                 | takes ownership of a local reference JNI returned          |
| `explicit GlobalRef(T)`       |            | ✓           |                 | `NewGlobalRef` from any reference                          |
| `explicit WeakGlobalRef(T)`   |            |             | ✓               | `NewWeakGlobalRef` from any reference                      |
| `get()`                       | ✓          | ✓           |                 | the raw `T` for a JNI call; ownership stays                |
| `explicit operator bool`      | ✓          | ✓           | ✓               | not empty                                                  |
| `reset()`                     | ✓          | ✓           | ✓               | delete the reference now, become empty                     |
| `release()`                   | ✓          |             |                 | give the reference away without deleting it                |
| `promote(JNIEnv*)`            |            |             | ✓               | a `LocalRef<T>` to the object, or an empty one if collected |
| destructor                    | ✓          | ✓           | ✓               | `reset()`                                                  |

`T` is the JNI type, so `LocalRef<jclass>::get()` returns `jclass` without a cast. `T` must convert to `jobject`
(checked with `static_assert`).

### 2. Move-only: one wrapper owns one JNI reference

Copying is deleted, moving is allowed and `noexcept`. Copying a raw handle would make two destructors delete the same
reference. After a move the source is empty and its destructor does nothing.

A second global reference to the same object is made explicitly: `GlobalRef<jobject> copy(original.get())`. Shared
ownership of one reference is `std::shared_ptr<GlobalRef<...>>`. Nothing is copied behind the caller's back.

### 3. `LocalRef`

- Wraps **only** what JNI returned to us (`FindClass`, `NewObject`, `Call*ObjectMethod`, `GetObjectArrayElement`,
  `NewStringUTF`, `NewLocalRef`). Parameters of a native method (`thiz`, arguments) are owned by the JVM and are not
  wrapped.
- **Stores the `JNIEnv*` it was given** and deletes through it. This is the one place where a `JNIEnv*` is stored,
  and it does not break ADR-0001 §3.1: a local reference and its `env` belong to the same thread and the same scope,
  and `LocalRef` lives on the stack of that thread. The destructor must not call `jnirt::env()`: after a detach it
  would attach the thread again just to delete a dead reference.
- The `env` is passed explicitly, so it is visible which thread owns the reference. Library code rarely writes this
  by hand: the typed calls of `class_cache` (P3.5) return `LocalRef<T>`.
- `release()` is how an object is **returned to Java**: `return result.release();`. Returning `result.get()` would
  delete the reference before the JVM reads it.

### 4. `GlobalRef`

- Does **not** store a `JNIEnv*`: a global reference is often created on one thread and destroyed on another (the
  last `shared_ptr` dies in a camera callback). Constructor and destructor use `jnirt::env()`. On an unknown native
  thread the destructor attaches it, and ADR-0003 detaches it at thread exit. Such a thread shows up in ANR traces;
  this is accepted.
- `GlobalRef(nullptr)` is an empty reference, not an error.
- A non-null reference for which `NewGlobalRef` returns `nullptr` (out of memory, or a collected weak reference passed
  as a raw handle) is `JNIRT_FATAL`. ART aborts on global table overflow anyway.

### 5. `WeakGlobalRef`

- The only way to reach the object is `promote(env)`, which is `NewLocalRef(weak)`: an atomic "check and hold". While
  the returned `LocalRef` is alive the GC does not collect the object.
- `promote` returns a `LocalRef`, not a `GlobalRef`: it is cheaper, and the object is almost always needed right
  there, in the current block.
- There is deliberately **no `get()` and no `isAlive()`**. Passing a raw weak handle to a JNI call, or checking
  liveness first and using the object later, are exactly the races this type exists to remove.
- Created and destroyed through `jnirt::env()`, like `GlobalRef`: a weak reference can die on any thread.
- Main use: a native object pointing back to its own Kotlin owner (ADR-0001 §1.1: Kotlin owns native, so a strong
  reference back would be a cycle through a GC root), and listeners in P7 (ADR-0001 §5.3).

### 6. Process shutdown (closes the P0 question)

- An ordinary `GlobalRef` is deleted in its destructor.
- **`GlobalRef` and `WeakGlobalRef` are not placed in `static` or global storage.** Static destructors run during
  `exit()`, when the VM may already be going away, and a `DeleteGlobalRef` there crashes the process on its way out.
- Long-lived references (classes, method IDs) live in `class_cache` (P3.5), which is **intentionally never destroyed**.
  The OS reclaims everything when the process dies.

### 7. Pending exceptions

`DeleteLocalRef`, `DeleteGlobalRef` and `DeleteWeakGlobalRef` are on JNI's list of calls allowed while an exception is
pending. So destructors that run during stack unwinding, or after a Java call threw, do not break ADR-0001 §6.5.

### 8. Misuse is reported (debug builds)

The checks exist only when `NDEBUG` is not defined. In release a `LocalRef` is two pointers and costs nothing beyond
the JNI calls themselves.

| Misuse                                                  | Caught by                                                  |
|---------------------------------------------------------|------------------------------------------------------------|
| `detachCurrentThread()` with a live `LocalRef` (ADR-0003 §5) | per-thread count of live `LocalRef`, read by `detachCurrentThread()` |
| `LocalRef` destroyed on another thread than its own     | the destructor compares its `env` with `GetEnv` of the current thread |
| `GlobalRef` / `WeakGlobalRef` leaked                    | process-wide counts of live references, compared by tests  |
| double delete, deleting a foreign reference             | move-only types; CheckJNI (`-Xcheck:jni`) in tests         |

```
jnirt: detachCurrentThread() called with 2 LocalRef still alive on thread "worker-3".
  why: local references become invalid after detach; their destructors would use a dead JNIEnv.
  fix: let LocalRef go out of scope before detaching (close the block first).

jnirt: a LocalRef is destroyed on thread "camera-rx", not on the thread that created it.
  why: a local reference belongs to the thread that created it; another thread can not delete it.
  fix: do not move LocalRef between threads; make a GlobalRef and pass that.
```

### 9. Not in P3.2

| What                                  | Why not now                                                                        |
|---------------------------------------|------------------------------------------------------------------------------------|
| copying `GlobalRef`                   | a hidden `NewGlobalRef` on every copy; an explicit copy or `shared_ptr` is clearer |
| `LocalFrame` (`PushLocalFrame`)       | `LocalRef` in a loop already keeps the table flat; add when needed (ADR-0001 §4.3) |
| non-owning view for parameters        | parameters stay raw `jobject`; the conversions of P5 (ADR-0008) give converted values instead |
| `operator->`, implicit conversion to `T` | implicit conversion makes it easy to hand out `get()` where `release()` is needed |
| typed object refs (`LocalRef<CameraJ>`) | compile-time object type checks; revisit with `class_cache`                     |

## Consequences

- Library code never calls `DeleteLocalRef` / `DeleteGlobalRef` / `DeleteWeakGlobalRef` directly.
- `jnirt_runtime` gets `src/ref.cpp` (non-template helpers and debug counters); the templates are header-only.
- Roadmap P0 "`GlobalRef` at process shutdown" is closed by §6.
- ADR-0003 §5 "detach with live `LocalRef`" is implemented here.

### Tests (host, real JVM, `-Xcheck:jni`)

| Check                                              | How                                                                  |
|----------------------------------------------------|----------------------------------------------------------------------|
| `LocalRef` deletes in its destructor               | 10 000 wrapped `NewStringUTF` in a loop; live count is 0 afterwards  |
| move                                               | source empty, the reference deleted exactly once                    |
| `release()`                                        | the reference is handed out, the destructor does not touch it       |
| `GlobalRef` destroyed on another thread            | moved into a `std::thread`; live count back to the start            |
| `GlobalRef(nullptr)`                               | empty, no JNI call                                                  |
| `promote()`                                        | non-empty while a strong reference exists; empty after the object is collected (`System.gc()` in a loop) |
| detach with a live `LocalRef`                      | death test with the message from §8                                 |
| `LocalRef` destroyed on another thread             | death test with the message from §8                                 |
