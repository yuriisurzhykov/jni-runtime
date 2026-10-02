# Callbacks and subscriptions by example

A learning guide to [ADR-0010](../adr/0010-callbacks-and-subscriptions.md): how a C++ thread calls a Kotlin listener safely,
and what `close()` does to a listener that is being called. The tests that run the pieces below are
`tests/host/support/listener_set_test.cpp`, `tests/host/runtime/listener_test.cpp` and
`android/src/test/kotlin/jnirt/SubscriptionTest.kt`.

## Why it is the hard one

Until now Kotlin called C++ on its own thread, and the JVM took care of everything around it. A callback is the other
way round: a camera thread, which the JVM has never seen, calls into Kotlin. Three lifetimes meet in one event (the
listener, the subscription, the owner) and two threads (the source and the receiver), and a bug shows once in thousands
of runs. So the design is mostly about **who may do what at the same time**.

## The example: frames from a camera

What the app writes:

```kotlin
camera.subscribe(mainExecutor, object : FrameListener {
    override fun onFrame(frame: ByteBuffer) { imageView.show(frame) }      // always on Main
    override fun onDisconnected(reason: String) { toast(reason) }
}).use { /* ... */ }                                                       // close() unsubscribes
```

The C++ core (no JNI) keeps a list of listeners and emits from its own thread:

```cpp
jnirt::ListenerSet<const Event &> events_;
jnirt::ListenerHandle subscribe(std::function<void(const Event &)> f) { return events_.add(std::move(f)); }
void onClose() noexcept { events_.close(); }                    // the owner is gone: drop every listener now
void run() { while (!closed_) { Frame f = recv(); events_.emit(Event{f}); } }     // no mutex of ours held here
```

The binding turns the Kotlin listener into such a `std::function` and the registration into a peer:

```cpp
Peer<Subscription> subscribe(JNIEnv *, jclass, Peer<Camera> self, Listener<FrameListenerJ> listener) {
    return makeSubscription(self->subscribe([listener](const Event &e) {                  // the lambda keeps the listener
        listener.call(FrameListenerJ::onFrame, NativeBuffer(e.frame, e.frame->data(), e.frame->size()));
    }));
}
```

And the Kotlin side wraps the app's listener so that the call hops to the executor chosen **at subscription**:

```kotlin
val delivery = Delivery(executor)
val forNative = object : FrameListener {                        // this is what native code calls, on the camera thread
    override fun onFrame(frame: ByteBuffer) = delivery.postLatest { listener.onFrame(frame) }
}
return Subscription(this, call { nativeSubscribe(it, forNative) }, delivery)
```

## The pieces, with Kotlin analogies

| Piece | In Kotlin terms |
|---|---|
| `ListenerSet` | `CopyOnWriteArrayList<(Event) -> Unit>` with an `active` flag per entry. `emit` walks a snapshot, so a listener may unsubscribe itself, or subscribe another, during the walk |
| `Listener<J>` | a strong reference to your listener (`GlobalRef`), plus "call it from any thread": attach the thread, open a local frame, call, log a Java exception instead of throwing |
| `Subscription` | `Closeable` / `Disposable`: closing it takes the listener out. It is a peer (P6), so it is idempotent and never waits |
| `Delivery` | `Handler.post` with a "still open?" check, before posting and again on the target thread. `postLatest` is `Flow.conflate()` |

Why the reference is **strong**: with a weak one, `camera.subscribe { ... }` would be collected at once (nobody else holds the
lambda) and the events would silently stop. The price is the usual one for any Android listener: unsubscribe in
`onStop`.

## What `close()` promises

It never waits for a running callback (the callback may be waiting for the very thread that closes). So:

- closed on the thread of a serial executor (Main): **no call of the listener after `close()` returned**, even those already
  queued;
- closed on another thread: no new call starts, queued events are dropped, but a call that is running right now may finish
  after `close()` returned.

## Rules for the C++ author

1. Call `emit` with none of your mutexes held. Copy the data under the lock, release, then emit.
2. The thread you start has no `entry()`: catch everything in its function, or an exception is `std::terminate`.
3. A worker thread does not own its object; the destructor joins it, and checks it is not running on that thread.
4. The `Executor` is a required parameter of `subscribe`. A direct executor (`Executor { it.run() }`) runs your listener
   on the camera thread; it must not wait for a thread that may be closing the camera.

## What to try

Run `ctest -R "ListenerSet|ListenerTest"` and read the stress test in `listener_set_test.cpp`: three threads emit, three
subscribe and cancel, then the list is closed. Build with `-DCMAKE_CXX_FLAGS=-fsanitize=thread` to see the same test under
ThreadSanitizer (it is clean).
