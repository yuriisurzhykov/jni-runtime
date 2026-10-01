# Threads, `JavaVM` and `JNIEnv` by example

A learning guide to [ADR-0003](../adr/0003-vm-and-env.md). One running example: the `wifi-cameras` library has a
camera that produces frames, and Kotlin has to be told about each one. For every situation you get the code, what the
runtime does inside, and a Kotlin analogy.

Some pieces come from later steps and are marked in comments: `GlobalRef` (P3.2), `class_cache` (P3.5),
`entry()` (P4). Only the `vm` / `env` part matters here.

## The two players

| JNI        | What it is                                                   | Kotlin / Android analogy                              |
|------------|--------------------------------------------------------------|-------------------------------------------------------|
| `JavaVM*`  | The virtual machine. One per process, lives forever          | `Application`: one instance, fine to keep globally    |
| `JNIEnv*`  | A thread's pass into the JVM. Every thread has its own       | `Looper.myLooper()`: bound to its thread, never handed to another |
| attach     | Register a native thread with the JVM and get its `JNIEnv*`  | `Looper.prepare()` on a thread that had none          |
| detach     | Unregister the thread before it dies                         | `looper.quit()`                                       |

Kotlin side shared by all examples:

```kotlin
interface FrameListener { fun onFrame(size: Int) }

@Suppress("KotlinJniMissingFunction") // natives are registered via RegisterNatives
internal class CameraNative {
    external fun connect(host: String): Long
    external fun startStream(handle: Long, listener: FrameListener)
}
```

---

## 1. Startup: `JNI_OnLoad`

```cpp
// unified/native/entry/jni_onload.cpp
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    return jnirt::init(vm, [](jnirt::Registry& r) {
        register_wificameras(r);
    });
}
```

Inside `init`: `setVm(vm)`, then registration and caches (P3.5, P3.6), then `return JNI_VERSION_1_6`.
From now on `jnirt::vm()` works on any thread.

Kotlin analogy: `Application.onCreate()`, where a singleton is initialized once.

---

## 2. Call from Kotlin: `env` is already there

```cpp
// bindings/CameraNative_jni.cpp
jlong connect(JNIEnv* env, jobject /*self*/, jstring host) {
    // env came as a parameter: use it. jnirt::env() is not needed here.
    auto h = jnirt::toString(env, host);              // P5
    return registry.add(std::make_shared<Camera>(h)); // P6
}
```

Inside: nothing. This is a Java thread (for example `Dispatchers.IO`), the JVM attached it already. If something
deeper calls `jnirt::env()`, it gets **the same** `env` from `GetEnv` and nothing is attached.

Rule: if `env` is in the parameters, use it. `jnirt::env()` is for places `env` cannot reach.

---

## 3. Your own native thread calls Kotlin

```cpp
void startStream(JNIEnv* env, jobject, jlong handle, jobject listener) {
    auto camera = registry.get(handle);
    jnirt::GlobalRef cb(env, listener);            // P3.2: the listener outlives this call

    std::thread([camera, cb = std::move(cb)] {
        pthread_setname_np(pthread_self(), "camera-rx");   // Java will see this name
        while (auto frame = camera->nextFrame()) {
            JNIEnv* env = jnirt::env();            // first call attaches; later calls are just GetEnv
            env->CallVoidMethod(cb.get(), ids.onFrame, (jint) frame->size());
            // P4: the runtime checks ExceptionCheck after the call
        }
    }).detach();                                   // std::thread::detach, not a JNI detach
}
```

Inside:

1. First `jnirt::env()` on the thread: `GetEnv` returns `JNI_EDETACHED`, so `AttachCurrentThread` with the name
   `camera-rx`, and the thread is marked as "attached by jnirt" in a `pthread_key`.
2. Following iterations: `GetEnv` returns OK, done. Cheap.
3. The thread function returns, the thread exits, the system runs the `pthread_key` destructor, which calls
   `DetachCurrentThread`.

Android Studio and ANR traces show the thread as `camera-rx`.

Kotlin analogy: `ThreadLocal.withInitial { attach() }` plus automatic cleanup when the thread dies.

---

## 4. A thread that belongs to a camera SDK

```cpp
// The vendor SDK creates its own thread and calls our callback. We do not own its loop.
void onSdkEvent(void* userData, int event) {
    auto* self = static_cast<CameraBinding*>(userData);
    JNIEnv* env = jnirt::env();                    // attaches if the SDK thread is not attached yet
    env->CallVoidMethod(self->listener.get(), ids.onEvent, event);
}
```

Inside: the same as example 3. The difference is that we do not know when the SDK ends the thread, and there is no
place to put a guard object. The `pthread_key` destructor runs when **any** thread exits, so the detach still
happens. If the SDK keeps the thread forever, it stays attached forever, which is fine.

---

## 5. Thread pool and manual detach

```cpp
void Worker::run() {                               // a long-lived thread of our pool
    while (auto task = queue_.take()) {
        {
            JNIEnv* env = jnirt::env();
            jnirt::LocalRef<jstring> s = ...;      // P3.2
            // ... work with Java
        }                                          // every LocalRef is gone here
    }
    jnirt::detachCurrentThread();                  // last statement: release the thread before it dies
}
```

Inside: the "attached by jnirt" mark is set, so `DetachCurrentThread` runs and the mark is cleared. When the thread
exits later, the `pthread_key` destructor sees an empty mark and does nothing. Had `detachCurrentThread()` been
forgotten, the detach would still happen at thread exit.

Calling `detachCurrentThread()` on a thread that is not attached does nothing.

---

## 6. Mistakes and what you will see

Every message has three parts: what happened, `why`, `fix`.

### 6.1 `jnirt::init` missing in `JNI_OnLoad`

```cpp
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) { return JNI_VERSION_1_6; }   // init is not called
```

The first `jnirt::env()` on a native thread:

```
F jnirt: jnirt is not initialized.
  why: jnirt::vm() was called before jnirt::init(vm, ...).
  fix: call jnirt::init(vm, ...) from JNI_OnLoad, see docs/integration.md.
```

The process aborts (`FATAL`); the text is in logcat and in the tombstone (`Abort message:`).

### 6.2 `JNIEnv*` taken to another thread

```cpp
void startStream(JNIEnv* env, jobject, jlong, jobject listener) {
    std::thread([env, listener] {                  // WRONG: env of another thread
        env->CallVoidMethod(listener, ids.onFrame, 0);
    }).detach();
}
```

CheckJNI catches this (host: `-Xcheck:jni`; device: `adb shell setprop debug.checkjni 1`):

```
JNI DETECTED ERROR IN APPLICATION: thread Thread[camera-rx] using JNIEnv* from thread Thread[DefaultDispatcher-worker-1]
```

Fix: call `jnirt::env()` inside the thread and keep the object in a `GlobalRef`. There is a second bug here too:
`listener` is a local ref and is dead once `startStream` returns. `GlobalRef` and move-only refs (P3.2) make such
code awkward to write.

### 6.3 Manual detach of a Java thread

```cpp
jlong connect(JNIEnv* env, jobject, jstring host) {
    ...
    jnirt::detachCurrentThread();                  // WRONG: this is a Java thread
}
```

```
IllegalStateException: thread "DefaultDispatcher-worker-1" was attached by the JVM, not by jnirt.
  fix: only threads that jnirt attached can be detached; remove this call.
```

This is `JNIRT_CHECK`: the call came from Kotlin, so after P4 you get a normal exception whose stack points at the
Kotlin line.

### 6.4 Detach with a live `LocalRef`

```cpp
jnirt::LocalRef<jstring> s = ...;
jnirt::detachCurrentThread();                      // WRONG: s is still alive
```

Debug build (per-thread `LocalRef` counter, P3.2):

```
F jnirt: detachCurrentThread() called with 1 LocalRef still alive on thread "worker-3".
  why: local references become invalid after detach; their destructors would use a dead JNIEnv.
  fix: let LocalRef go out of scope before detaching (close the block first).
```

### 6.5 `FindClass` on a native thread

```cpp
std::thread([] {
    jclass c = jnirt::env()->FindClass("com/example/FrameListener");   // returns null
}).detach();
```

A native thread uses the system class loader, which cannot see app classes. Two barriers:

- a CI check: raw `FindClass` / `GetMethodID` are not allowed in library code, everything goes through
  `class_cache` (P3.5);
- `jnirt::findClass` checks the thread in debug builds:
  ```
  F jnirt: class lookup "com/example/FrameListener" on thread "camera-rx" (attached by jnirt).
    why: native threads use the system class loader and can not see app classes.
    fix: declare the class in class_cache; it is resolved in JNI_OnLoad.
  ```

---

## Summary

| Situation                         | What you write                            | What the runtime does                      |
|-----------------------------------|-------------------------------------------|--------------------------------------------|
| Call from Kotlin                  | use `env` from the parameters             | nothing                                    |
| Your own `std::thread`            | `jnirt::env()`                            | attach on first call, detach at thread exit |
| A thread owned by an SDK          | `jnirt::env()`                            | the same                                   |
| Pool thread, release early        | `jnirt::detachCurrentThread()` at the end | detach now, nothing at exit                |
| Misuse                            | nothing                                   | fails with a what / why / fix message      |
