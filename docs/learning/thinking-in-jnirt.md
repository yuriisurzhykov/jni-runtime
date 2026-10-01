# Thinking in jnirt

How to think when you write native code on top of jni-runtime. This is not an API reference: the ADRs are. It is the
set of questions to ask yourself, so that the right code follows from the answers. Every idea comes with a Kotlin
analogy, because the rules on the native side are the same ones you already follow in Android, only written down
explicitly.

Related: [ADR-0001](../adr/0001-invariants.md) (invariants), [ADR-0002](../adr/0002-errors-and-asserts.md) (errors),
[ADR-0003](../adr/0003-vm-and-env.md) and [vm-env-by-example](vm-env-by-example.md) (threads),
[ADR-0004](../adr/0004-references.md) (references).

---

## 1. Two worlds, one border

There is the Kotlin world with a garbage collector, and the C++ world where every object has an owner and dies at a
known moment. JNI is the border between them, and nothing crosses it by itself: every Java object you touch from C++
is reached through a **reference**, and every reference has a lifetime that someone has to end.

So the habit to build is: **whenever a `jobject` appears, ask who owns it and how long it lives.** In Kotlin the GC
answers that question for you. On the native side jnirt answers it, but only if you choose the right type.

## 2. Ownership points from Kotlin to native

A Kotlin object owns its native object (ADR-0001 §1.1), never the other way round.

```
Kotlin Camera ──owns──► NativeCamera (C++)
       ▲                       │
       └──── weak, never strong┘
```

Kotlin analogy: an `Activity` owns its `ViewModel`, and a `ViewModel` must never hold the `Activity`. If it does,
the `Activity` leaks. Same here: if native code holds its Kotlin owner strongly, the pair becomes a cycle through a
GC root and is never collected. That is why a native object points back with a `WeakGlobalRef`.

## 3. Which reference do I need?

One question decides it: **how long do I need this object, and who owns it?**

| I need the object...                                     | Use                    | Kotlin analogy                         |
|----------------------------------------------------------|------------------------|----------------------------------------|
| only inside this native call, and JVM gave it to me (parameter) | nothing, the raw `jobject` | a function parameter              |
| only inside this block, and JNI returned it to me        | `LocalRef<T>`          | a local `val` inside `use {}`          |
| after this call returns, and I own it (listener, callback) | `GlobalRef<T>`       | a field holding a strong reference     |
| after this call returns, but someone else owns it (my Kotlin owner) | `WeakGlobalRef<T>` | `WeakReference<T>`              |
| to hand it back to Kotlin as the return value            | `LocalRef<T>` + `release()` | `return obj`                      |

The last row is the one that bites: a `LocalRef` deletes its reference when it dies, so `return ref.get()` returns a
reference that is already gone. `return ref.release()` gives it to the JVM.

## 4. Move, don't copy

`LocalRef`, `GlobalRef` and `WeakGlobalRef` can be moved but not copied. If the compiler says "copy constructor is
deleted", it is asking you a question: **who owns this now?**

- If ownership moves (you store it in a struct, return it from a function), write `std::move(ref)`. The old variable
  becomes empty.
- If two places really need the object, they are two owners: make a second reference explicitly
  (`GlobalRef<jobject> second(first.get())`) or share one with `std::shared_ptr<GlobalRef<...>>`.

Kotlin analogy: a key to a flat. You can hand it over, and then you no longer have it. You can not silently make a
duplicate: you have to go and order one.

## 5. Threads: where does `env` come from?

A `JNIEnv*` is a thread's pass into the JVM, like `Looper.myLooper()`: it belongs to one thread and is never handed
to another.

- The function received `env` as a parameter: use it.
- It did not (a native thread, a callback, a destructor): call `jnirt::env()`. It attaches the thread if needed, and
  jnirt detaches it when the thread exits.
- Never store `env` in a field or pass it to another thread. The only thing that stores it is `LocalRef`, and only
  because it lives and dies on that same thread.

The same split applies to references: a `LocalRef` is as thread-bound as `env`, a `GlobalRef` / `WeakGlobalRef` can be
used and destroyed on any thread. **Crossing a thread means a global reference.**

## 6. Loops and long native methods

Every local reference made inside a loop must die inside the same iteration. With `LocalRef` declared inside the loop
body this happens automatically:

```cpp
for (jsize i = 0; i < count; ++i) {
    jnirt::LocalRef<jobject> item(env, env->GetObjectArrayElement(array, i));
    process(env, item.get());
}   // deleted here, every iteration
```

Kotlin has nothing similar because the GC cleans up; here the table of local references is small (about 512 on ART)
and a long loop without cleanup crashes.

## 7. Look things up once, at startup

`FindClass`, `GetMethodID` and `GetFieldID` belong in `JNI_OnLoad`, never in a hot path and never on a native thread
(there `FindClass` sees only system classes). jnirt's `class_cache` (P3.5) does this for you from a declaration of
the Kotlin class: you describe what you call, the library finds it once and fails at startup with the exact name if
something is missing. Think of it as a Retrofit interface for a Kotlin class.

## 8. Decide whose fault a failure is

ADR-0002 in one line each:

- the caller passed something wrong or called at the wrong time: `JNIRT_REQUIRE` / `JNIRT_CHECK`, Kotlin gets
  `IllegalArgumentException` / `IllegalStateException`;
- the outside world failed (network, device): `throw jnirt::Error(...)`, Kotlin gets a typed error;
- our own code is broken: `JNIRT_ASSERT` / `JNIRT_FATAL`, the process crashes with a tombstone.

Do not catch to hide. Do not crash for things the caller can fix.

## 9. Let the library shout

jnirt checks what it can and tells you in three lines: what happened, why it is bad, how to fix it. Treat these like
`StrictMode` violations: they are bugs that would otherwise show up as rare crashes in production. Run tests and
debug builds with CheckJNI on; it catches what jnirt can not (a `JNIEnv*` used on the wrong thread, a wrong type
passed to a JNI call).

## 10. Checklist before you commit native code

- Every `jobject` I created or got from JNI is inside a `LocalRef` / `GlobalRef` / `WeakGlobalRef`.
- Nothing I keep after the call returns is a `LocalRef` or a raw `jobject`.
- My native object refers to its Kotlin owner weakly.
- No `GlobalRef` in a `static` or global variable (long-lived references live in `class_cache`).
- Objects returned to Kotlin go through `release()`.
- No `JNIEnv*` in fields, no `LocalRef` moved to another thread.
- No `FindClass` / `GetMethodID` outside startup.
- Each failure is classified: caller's mistake, outside world, or our bug.

## Common mistakes

| Mistake                                              | What happens                                  | Fix                                             |
|------------------------------------------------------|-----------------------------------------------|-------------------------------------------------|
| keeping `thiz` in a struct for later                 | the reference dies when the call returns; later use crashes | `WeakGlobalRef<jobject>(thiz)`        |
| `return ref.get();` from a native method             | Kotlin receives a deleted reference           | `return ref.release();`                         |
| a `LocalRef` member of an object used on several threads | wrong-thread delete, caught in debug       | `GlobalRef`                                     |
| holding the Kotlin owner with a `GlobalRef`          | the owner and its native object never get collected | `WeakGlobalRef` + `promote(env)`          |
| `static GlobalRef<jclass> gClass;`                   | crash during `exit()`                         | describe the class in `class_cache`             |
| `detachCurrentThread()` with a `LocalRef` still in scope | debug crash with a message                | close the block before detaching                |
