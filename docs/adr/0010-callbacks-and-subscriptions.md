# ADR-0010 - Callbacks and subscriptions: native code calls Kotlin

## Status

Proposed. Implementation: roadmap P7.1 to P7.4. Design discussion: `design/p7-design.md` in the project files.
Walkthrough with an example: [learning/callbacks-by-example.md](../learning/callbacks-by-example.md).

## Context

Until now Kotlin called C++ **on its own thread**: the JVM gave `env`, owned the references and turned exceptions into
Kotlin exceptions. A callback is the other direction: C++ calls Kotlin **on a thread of its own** (a camera thread, an
SDK thread) that the JVM knows nothing about. Every guarantee that was free is gone:

| What breaks                                                                       | How it looks                                  | Closed by                                      |
|-----------------------------------------------------------------------------------|-----------------------------------------------|------------------------------------------------|
| `env` of one thread used on another                                               | CheckJNI abort, SIGSEGV                       | `jnirt::env()` (ADR-0003)                      |
| thread not attached / not detached                                                | crash at thread exit                          | `env()` + `pthread_key` (ADR-0003)             |
| the `jobject` of `subscribe(listener)` is a local reference, gone after the call  | "use of deleted local reference"              | `GlobalRef` (ADR-0004), `Listener<J>` (§3)     |
| `FindClass` on a native thread uses the system class loader                       | `ClassNotFoundException`                      | `class_cache` at startup (ADR-0006)            |
| who holds the listener: weak is collected at once, strong without `close()` leaks | "no events, nobody knows why" / a leaked Activity | §2                                         |
| unsubscribe on one thread while another calls; a callback unsubscribes itself     | use-after-free; `ConcurrentModificationException` in Kotlin | `ListenerSet` (§3)             |
| a callback called under a lock, and calling back into the library                 | deadlock                                      | ADR-0001 §3.4, `ListenerSet` holds no lock    |
| owner closed during a callback: the destructor joins a thread that waits for Main, or joins itself | deadlock, `std::terminate`   | mandatory `Executor` (§5), rules (§8)          |
| a Java exception left pending, then another JNI call                              | abort                                         | typed calls (ADR-0006), catch per listener (§7) |
| local references of a long-lived native thread are never freed                    | "local reference table overflow" after ~512 events | local frame per event (§3)                |
| callback arrives on the wrong thread                                              | `CalledFromWrongThreadException`              | `Executor` captured at subscription (§5)       |
| slow consumer, fast source                                                        | the queue grows, OOM                          | delivery policy (§9)                           |

## Decision

### 1. Layers

```
Kotlin Camera ──owns──> native Camera ──owns──> ListenerSet ──> entry ──> Listener<J> ──GlobalRef──> wrapper ──> app listener
      ^                                                                                              └─> Delivery(executor)
      └── Subscription (a peer: its own handle, onClose = take the entry out)
```

- `ListenerSet` (C++, `support`, no JNI): the list of listeners of an event source.
- `Listener<J>` (C++, `runtime`): a Kotlin object held by native code, called from any thread.
- `Subscription` (peer, ADR-0009 §7): one registration. Kotlin side `jnirt.Subscription : NativePeer`.
- `Delivery` (Kotlin): the executor of one subscription and its "open" flag.

### 2. The listener is held strongly

Native holds a **strong** `GlobalRef` to the Kotlin listener while the subscription is open. This replaces "weak global
ref and a liveness check" of ADR-0001 §5.3. A weak reference breaks lambdas: `camera.subscribe { ... }` is referenced by
nobody else, the garbage collector takes it, and events silently stop (the known trap of
`SharedPreferences.registerOnSharedPreferenceChangeListener`). A strong reference is what `View.setOnClickListener`,
`LocationManager.requestLocationUpdates` and `Flow.collect` do. The cost is the usual one: a listener that captures an
`Activity` keeps it until the subscription is closed; a listener that captures its own owner keeps the owner away from
the leak guard. Instead of a liveness check each call looks at the entry's `active` flag.

`WeakGlobalRef` stays for the other use (native object to its Kotlin owner, ADR-0004 §5). A weak listener, if ever
needed, is an explicit parameter, not now.

### 3. `ListenerSet` and `Listener<J>`

`jnirt::ListenerSet<Args...>` (`<jnirt/support/listener_set.h>`): `add(fn)` returns a `ListenerHandle`, `emit(args...)`,
`close()`. A `CopyOnWriteArrayList` with an `active` flag on each entry:

- `emit` takes the current list under a mutex (a pointer copy) and walks it **with no lock held**; the list is replaced
  as a whole, never changed in place.
- `ListenerHandle::cancel()` marks the entry inactive and removes it from the list, from any thread, also from inside a
  listener. A listener added during `emit` gets the next event, one removed during `emit` is skipped if its turn has not
  come. A call that has already started runs to its end.
- `close()` drops every listener (their `GlobalRef`s go with them) and makes `add` throw `IllegalState`: a listener added
  to a dead owner would never be called and never released. A handle holds the list weakly: it may outlive the list.
- A listener that throws (any C++ exception) is logged and the others still get the event. A native thread has no
  `entry()` (ADR-0007), so `emit` is the catch.

`jnirt::Listener<J>` (`<jnirt/runtime/listener.h>`), `J` the description of the Java interface:

- A parameter of a native (`Listener<FrameListenerJ>`, Java `FrameListener`): a `GlobalRef` in a `shared_ptr`, so that
  copies are cheap and the lambda that holds it can be copied (`std::function`). `null` is `IllegalArgumentException`.
  A row of the table (ADR-0008) for parameters only; as a result or an argument of a call into Java it does not compile.
- `listener.call(FrameListenerJ::onFrame, args...)` works on any thread: `jnirt::env()`, then `PushLocalFrame(16)` (the
  references of one event go with the event; a thread outside a Java call never frees them itself), then the typed call
  of ADR-0006, then `PopLocalFrame`. A `JavaException` is logged, not thrown (§7).
- Only interfaces, not `Function1`: a Kotlin lambda type is `invoke(Object): Object` with boxing and erased generics. A
  `fun interface` makes lambdas work.
- `NativeBuffer` may now be an **argument** of a call into Java (a frame handed to a listener: one new `ByteBuffer` per
  call, which is what P6.5 does for a result). `Peer<T>` stays a native parameter or result only. Row flag
  `allowedAsArgument`.

### 4. `Subscription` is a peer

As in ADR-0009 §7. Native: `jnirt::Subscription` holds a `ListenerHandle`; `onClose()` cancels it; `makeSubscription(handle)`
makes the peer. Kotlin: `jnirt.Subscription(owner, handle, delivery) : NativePeer`, so `close()` is idempotent, never
waits, and `use { }` works.

The owner closes its subscriptions in two places: natively, its `onClose()` calls `ListenerSet::close()` (entries and
`GlobalRef`s are released at once); in Kotlin, `NativePeer` keeps the subscriptions made on it (`adopt`, `forget`) and
`close()` closes the rest. `adopt` on an owner that is already closed closes the subscription at once.

### 5. `Delivery`: the thread of the listener

```kotlin
fun subscribe(executor: Executor, listener: FrameListener): Subscription {
    val delivery = Delivery(executor)                       // captured here, at subscription time
    val forNative = object : FrameListener {                // what native calls, on its own thread
        override fun onFrame(frame: ByteBuffer) = delivery.postLatest { listener.onFrame(frame) }
    }
    return Subscription(this, call { nativeSubscribe(it, forNative) }, delivery)
}
```

- The **`Executor` is mandatory** in the API of a library: no default, a direct executor (`Executor { it.run() }`) has to be
  written out. Otherwise user code runs on the native thread, where it can block, or close the owner from the owner's own
  thread. `Handler` is `Executor { handler.post(it) }`. `Looper.myLooper()` inside a callback would be `null` on a native
  thread, hence ADR-0001 §3.5: capture at subscription.
- `Delivery.post` runs the block on the executor if the subscription is still open, and checks again when the block's turn
  comes. `shutdown()` is the first thing `Subscription.close()` does.
- The wrapper (`forNative`) is a few lines per method, written by the library (Kotlin `by` can not add the hop). Its
  methods are found by name from native code: the library keeps the interface (`consumer-rules.pro`, the self-check of
  ADR-0006 reports a missing one at startup).

### 6. What `close()` guarantees

`close()` never waits for a running callback (ADR-0001 §2.3): a callback may wait for the thread that is closing.

| Who closes, and where                                                      | After `close()` returned                                         |
|----------------------------------------------------------------------------|------------------------------------------------------------------|
| `Subscription` or owner, on the thread of a **serial** executor (Main, Handler, single thread) | no call of the listener at all, queued events dropped |
| any other thread                                                           | no new call starts, queued events dropped; a call already running may finish later |
| `Subscription.close()` inside its own listener                             | works; the current call finishes                                 |
| owner `close()` inside a callback running on the owner's thread (direct executor) | forbidden, see §8                                          |

This is the guarantee of `Handler.removeCallbacks` and `LiveData.removeObserver`.

### 7. Exceptions in a listener

Native: `Listener::call` catches `JavaException` and logs it at ERROR with `Throwable.toString()`; `emit` catches anything
else. The other listeners get the event. Kotlin: `Delivery` catches nothing. With a `Handler` the exception crashes the
app like any bug in a callback; with a direct executor it reaches native code and is logged. (Android's own rule: a bug
on the main thread crashes, a bug on a foreign thread is a log line. Forwarding to `Thread.uncaughtExceptionHandler` is one
line in `Delivery` if wanted.)

### 8. Rules for the author of `core/`

- Call `emit` **with none of your own mutexes held**: copy the data under the lock, release, then emit.
- A worker thread does not own its object (ADR-0009 §3); emit the events of one source from one thread, or say in the
  class's documentation that they come from several.
- The function of a thread you start has **no `entry()`**: catch everything in it. An exception that leaves it is
  `std::terminate`. `emit` catches only the listeners'.
- A destructor that joins a worker must not run on that worker: a listener on a direct executor that closes the owner,
  when that was the last reference, would make the thread join itself and then go on using a destroyed object. Check
  `std::this_thread::get_id()` and fail with `JNIRT_FATAL` and a clear message (`JNIRT_CHECK` throws, and a destructor
  does not throw).
- A direct-executor listener must not wait for another thread that may be closing the owner.

### 9. A slow consumer

The source emits every event; what to do with a queue is the business of `Delivery`:

| Method          | Queue                                              | Kotlin analogy                           |
|-----------------|----------------------------------------------------|------------------------------------------|
| `post`          | unbounded, every event, in order                   | `Channel(UNLIMITED)`                     |
| `postLatest`    | one waiting event, replaced by the next            | `Flow.conflate()` (video)                |
| `postBuffered`  | at most `backlog` waiting, the oldest is dropped   | `buffer(n, DROP_OLDEST)`                 |

A `Flow` adapter is an extension in the application or library (`callbackFlow { subscribe(...); awaitClose { subscription.close() } }`),
not in jnirt: jnirt has no dependency on coroutines. `examples/jni-modules-p7/.../CameraFlows.kt` is the reference: the end of
collection closes the subscription, `onDisconnected` ends the flow with an exception.

**Measured (P7.5), `tests/host/runtime/frame_memory_bench.cpp`:** a `ByteBuffer` made by `NewDirectByteBuffer` does not tell
the garbage collector how big the memory behind it is. Frames of 1 MiB dropped by a slow consumer on a small Java heap piled
up to **3000 MiB** of native memory before the collector took them. The risk is real, not theoretical.

**Mitigation (done):** the runtime counts the bytes of live `NativeBuffer`s (the size is part of the owner kept in the peer
table). Above 64 MiB, `newNativeBuffer` calls `System.gc()`, at most once per 200 ms. The same run peaks at **83 MiB**. The
numbers are the property of the test (`DroppedFramesDoNotPileUpNativeMemory` asserts a bound), not of a device: `System.gc()`
on ART is a request, and its cost on a phone is not measured. The threshold is `setBufferGcThreshold` (internal, for tests).
`NativeAllocationRegistry` is not used: it is public only since API 31, and the counter works on every API level the library supports.

### 10. Not in P7

- The reverse direction of the roadmap item (C++ giving Kotlin something callable): Kotlin calls C++ with a native method
  and a handle; `Runnable { camera.nativeFoo(handle) }` is one line in Kotlin.
- `Function1`, `suspend`, `Flow` on the border (the adapter is an extension, see §9); a weak listener.

## Consequences

- A library writes: a Kotlin interface, a wrapper over it with `delivery.post`, `subscribe`; a core with a `ListenerSet`
  member and `onClose() { events_.close(); }`; one native `subscribe` with a `Listener<J>` parameter.
- The twelve breakages above have an owner each; four of them (§2, `ListenerSet`, §5, §9) are design, not checks.
- `NativeBuffer` loses "result only".

## Changes to earlier documents

- ADR-0001 §5.3: weak reference replaced by a strong one (§2). §3.5: the `Executor` is mandatory.
- ADR-0009 §9: `NativeBuffer` also as an argument of a call into Java. §7: the owner closes subscriptions natively and in Kotlin.
- ADR-0008 "Not in this step": `Param<ListenerJ>` is `Listener<J>`.

## Tests

| What                                                                                          | Where                          |
|-----------------------------------------------------------------------------------------------|--------------------------------|
| order, cancel (twice, inside itself, of a later one), add during emit, close inside emit, add after close, handle after the list is gone, a throwing listener, the listener destroyed on cancel/close | `listener_set_test`     |
| emit, subscribe, cancel and close from several threads at once, ThreadSanitizer clean          | `listener_set_test` (stress)   |
| events reach a Java listener; closing the subscription stops them and frees the `GlobalRef`; closing the owner frees it at once | `listener_test` (real JVM) |
| a throwing Java listener does not stop the next; 5000 events from a native thread leave no local reference; `NativeBuffer` as an argument | `listener_test` (real JVM) |
| `Listener` as a result or an argument of a call into Java does not compile                     | `compile_fail/listener_to_java` |
| `Delivery`: runs on the executor, order, `shutdown` drops queued events, `postLatest`, `postBuffered` (bound, order, shutdown); `Subscription`: close, owner closes subscriptions, adopt on a closed owner, close inside the listener | `android/src/test` (`SubscriptionTest`) |
| dropped 1 MiB frames do not pile up native memory (bound asserted; the bench prints the numbers)  | `listener_test`, `frame_memory_bench` |
| the whole path on a computer's JVM: threads, close, throwing listener, bounded backlog, `Flow`  | `examples/jni-modules-p7/host-check` |
