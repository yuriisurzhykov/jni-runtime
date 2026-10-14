# ADR-0009 - Peers: a Kotlin object that owns a native object

## Status

Accepted. Implemented in roadmap P6.1 to P6.5. Design discussion:
`design/p6-design.md` in the project files.

## Context

A C++ object that lives longer than one call (a camera connection, a decoder) has to be kept somewhere between calls from
Kotlin. The usual way on Android (`MediaPlayer.mNativeContext`) is to give Kotlin a pointer as a `Long` and take it back
on every call. It breaks in four ways, and each one takes down the whole process, not only the camera:

| Mistake                                        | With a raw pointer                             | With a peer                                                |
|------------------------------------------------|------------------------------------------------|------------------------------------------------------------|
| `start()` after `close()`                      | read of freed memory, SIGSEGV                  | `IllegalStateException`                                    |
| `close()` twice                                | double free                                    | the second call does nothing                               |
| `close()` on one thread, `read()` on another   | the object is deleted under the running code   | `read()` wakes up with `IllegalStateException`             |
| `close()` forgotten                            | the socket leaks for the life of the process   | a safety net closes it after GC, a warning in debug        |

ADR-0001 already fixed the rules (§1 ownership, §2 `close()` against calls in flight, §3.4 no calls under a lock, §8.1
no handle crosses a library boundary). This ADR chooses the mechanism. The words *peer*, *handle* and *in-flight call*
are those of the glossary of ADR-0001.

One requirement is stricter than ADR-0001 §2.2 on its own: **`close()` frees the resource now, or within microseconds**.
It does not wait for a blocked call to finish, because a blocked `read()` may never finish. Java's `Socket.close()` is
the model: closing a socket wakes a thread blocked in `read()` on it.

## Decision

### 1. The table of peers

One table per process, in the runtime (`jnirt::PeerTable`, `<jnirt/runtime/peer.h>`):

```
handle (jlong)  ->  { shared_ptr<void> object, type tag, optional onClose hook }
```

- A handle is a counter that starts at 1 and never goes back; `0` means "no peer". A handle that was closed can never
  find another object. This is not an address (ADR-0001 §1.2).
- The type tag is the address of a per-type static. `acquire<Player>(handle of a Camera)` is an `IllegalState`, not a
  wrong cast.
- One table, not one per type: the rules of closing, the checks and the safety net are the same for every library and
  would be copied into each. The tag keeps types apart.
- A mutex guards **only** the lookup, the insert and the erase. A native call, `onClose` and a destructor all run with no
  lock held (ADR-0001 §3.4). The cost of the lock is tens of nanoseconds against a JNI crossing; if it ever shows,
  sharding the table is a change inside it, the API stays.
- The table is never destroyed at process exit (the same rule as `GlobalRef`, ADR-0004).

### 2. Three operations

```cpp
jlong PeerTable::add(std::shared_ptr<T>);        // new handle
Peer<T> PeerTable::acquire<T>(jlong);            // IllegalState if the handle is unknown, closed, or of another type
void PeerTable::close(jlong) noexcept;           // see §3; unknown or closed: does nothing
```

`acquire` hands out a `shared_ptr`: while a native call holds it, the object cannot be destroyed (ADR-0001 §2.1).

### 3. close(): the resource now, the memory later

`PeerTable::close(handle)`:

1. under the mutex, erase the entry (unknown handle: return, which is what makes `close()` idempotent);
2. **outside** the mutex, call the object's hook if it has one;
3. drop the table's reference. The destructor runs on the thread that drops the last one: here if no call is in flight,
   otherwise at the end of the last call.

The hook is an optional member of the C++ class, found at compile time: `void onClose() noexcept`, or
`void close() noexcept` when the author prefers that name (`onClose` wins if both exist). Its contract:

> It may be called from any thread while other threads are inside any method of the object. It **must not wait** (no
> `join`, nothing long). It tells everybody to stop and wakes the sleepers: shut the socket down, set a flag, notify.

A class that blocks (network, a queue, a timer) must have a hook. A class that never blocks needs none: the reference
count alone is enough.

Rules for the author of `core/`, written here because they are where the mistakes happen:

- **Release in two steps.** The hook does `shutdown(fd)`; the destructor does `close(fd)`. Closing a descriptor while
  another thread is inside `recv` on it lets the system give the same number to another file.
- **A worker thread of the object must not own the object.** A `shared_ptr` held by the object's own thread keeps the
  object alive forever and may run the destructor on that thread, which then joins itself. Workers hold `this` and stop
  on the flag the hook sets; the destructor joins them.
- **The destructor does not throw and does not call Java directly.** A `GlobalRef` is safe in a destructor
  (ADR-0003/0004), which also covers a destructor that runs on a thread the JVM has not seen.

### 4. Peer<T> in the conversion table (ADR-0008)

```cpp
Peer<Camera> open(JNIEnv *, jclass, std::string url) { return makePeer<Camera>(url); }       // Kotlin: Long
std::vector<jbyte> read(JNIEnv *, jclass, Peer<Camera> self) { return self->read(); }        // Kotlin: Long
```

`Peer<T>` is a thin type around `shared_ptr<T>` (`->`, `*`, `get()`), not a bare `shared_ptr`: an arbitrary
`shared_ptr` in a signature must not silently become a handle. Its row is `Jni = jlong`, descriptor `J`:

- as a **parameter**, `fromJava` is `PeerTable::acquire<T>`. It runs inside the try of `entry()` before the body, so the
  object is held for the whole call with nothing for the library author to remember, and a closed handle is an
  `IllegalStateException` in Kotlin.
- as a **result**, `toJava` is `PeerTable::add`: the new object gets its handle on its way out. If the constructor
  throws, no handle exists, and Kotlin has no object.

A `Peer<T>` is good for a native parameter and a native result. Not as an element of a collection, an optional, or a
field (a static_assert says so): one handle, one owner.

### 5. Closing from Kotlin

`jnirt.NativePeer` is the base class of every peer (`Closeable`). It keeps the handle in an `AtomicLong` inside a small
`PeerState` object:

```kotlin
abstract class NativePeer protected constructor(handle: Long) : Closeable {
    protected fun <R> call(block: (handle: Long) -> R): R   // IllegalStateException if closed
    override fun close()                                    // getAndSet(0): one thread wins, so close() is idempotent
    val isClosed: Boolean
}
```

- The handle goes to native as a parameter (`call { nativeRead(it) }`), it is not read from a field there: fewer JNI calls
  and no race between a check and a use. If `close()` wins between `call` and the native, the table says no
  (`IllegalStateException`), never a crash.
- `close()` calls `NativePeerNatives.close(handle)`, the one native method jnirt registers for peers (as it does for
  `NativeLogBridge.setNativeLevel`). It calls `PeerTable::close`; it does not block (ADR-0001 §2.3).
- A library writes `fun read() = call { nativeRead(it) }` and a factory `Camera(nativeOpen(url))`.

### 6. The safety net

Behind a Kotlin interface (ADR-0001 §1.4):

```kotlin
interface LeakGuard { fun watch(owner: Any, state: PeerState) }
```

- `CleanerLeakGuard` on API 33 and later: `java.lang.ref.Cleaner`.
- `ReferenceQueueLeakGuard` below (API 26): `PhantomReference` plus a daemon thread.
- The registered action holds the `PeerState` (a `Long` inside), **never the peer itself**, or the peer would never become
  unreachable.
- When the action finds the peer still open it closes it, and warns with the class name ("Camera was not closed")
  if the app asked for warnings: `LeakGuards.warnOnLeak = BuildConfig.DEBUG`. Off by default, so a release build closes
  silently; the warning goes to `LeakGuards.warning` (stderr by default, replace it to use Timber). `finalize()` stays
  forbidden.
- The implementation is picked once at startup and can be replaced, so tests do not wait for a GC.

### 7. Subscriptions are peers (P7, ADR-0010)

A `Subscription` (ADR-0001 §5) is a peer on the same table: its native part is the registration of a listener, its
`onClose` unregisters it. An owner closes its subscriptions from its own `onClose`. There is no second table and no
second set of rules.

### 8. Errors

| Situation                                              | What Kotlin gets                                                      |
|--------------------------------------------------------|-----------------------------------------------------------------------|
| a call on a closed peer (Kotlin check)                 | `IllegalStateException`                                               |
| a call whose handle was closed after the Kotlin check  | `IllegalStateException` (from `acquire`)                              |
| a handle of another type                               | `IllegalStateException`                                               |
| a call woken by `close()` (`onClose` made it stop)     | whatever the class throws; the convention is `IllegalState` ("closed") |
| `close()` of a closed peer                             | nothing                                                               |

These are programmer errors in the sense of ADR-0002: always thrown, release and debug alike. No `(file:line)` is added
to the ones jnirt raises, as for the conversion errors (ADR-0008 §8).

### 9. Native memory given to Kotlin as a ByteBuffer (P6.5)

For data that is big and frequent (the frames of a video stream) copying into a `ByteArray` costs more than it should.
A native function may return a `NativeBuffer` (`<jnirt/runtime/native_buffer.h>`), and Kotlin gets a direct
`java.nio.ByteBuffer` over the same bytes:

```cpp
NativeBuffer frame(JNIEnv *, jclass, Peer<Camera> self) { return self->nextFrame(); }   // Kotlin: ByteBuffer
```

- A `NativeBuffer` is `{ shared_ptr<void> owner, data, size }`: `owner` keeps the bytes alive (a vector, a pool slot, the
  camera), `NativeBuffer::allocate(size)` and `copyOf(bytes, size)` make the common cases.
- The memory is registered in the table of peers, as a peer without a hook, and `NativeBuffers.watch(buffer, handle)`
  (Kotlin, called from native) ties the handle to the buffer with the same `LeakGuard` that watches forgotten peers. The
  guard closes the handle when the garbage collector collects the `ByteBuffer`, and it does not warn: that is how
  these buffers end.
- **Closing the peer that made the frame does not invalidate it.** A frame lives while Kotlin holds it. A library that
  wants the frame to keep its camera alive passes the camera as `owner`.
- **One buffer per frame.** Simple, and the memory is released by itself. A shared ring buffer with slices is a
  possible later optimisation, to be chosen after measuring on a real stream, not before. Nothing around it changes.
- A result of a native, or (ADR-0010 §3) an argument of a call into Java; `Peer<T>` is a result only. A static_assert for
  a parameter, an `std::optional`, a collection, a field. A `ByteBuffer` that Kotlin gives to native is read with `DirectBuffer`
  (ADR-0008), which borrows it for one call.
- More than 2 GiB - 1 bytes is an `IllegalArgumentException` (a `ByteBuffer` cannot hold more). An empty buffer is
  allowed. If the `ByteBuffer` cannot be created, the handle is closed again and the error goes to Kotlin.
- The bytes are big-endian when read as `Int` or `Long`, like every `ByteBuffer`; Kotlin changes that with `order()`.
  Kotlin must not write into a frame that native code still writes to: shared memory, shared rules.

### 10. Not in P6

- **Typed object parameter for a Kotlin listener (`Param<ListenerJ>`)**: P7.

## Consequences

- The four crashes of the table in Context become exceptions or no-ops; a forgotten `close()` is closed after GC.
- A library author writes a plain C++ class, an optional `onClose()`, two natives and a Kotlin subclass of `NativePeer`.
  The reference counting, the lookup and the checks are not theirs.
- A blocking class must write `onClose()`. Without it `close()` still returns at once, but the blocked call is not
  woken and the memory waits for it. This is the one thing the runtime cannot do for the author, so it is in the
  rules of §3 and in the learning guide.
- The destructor can run on any thread that has called into the object (or, with P7, a native thread). §3 says what it
  may do.
- One process-wide mutex on every peer call: acceptable for coarse-grained calls (ADR-0001 §7.3).

## Changes to earlier documents

- ADR-0001 §2.2: "`close()` marks the peer closed" becomes "marks the peer closed and calls its `onClose` hook, so the
  resource is released at once; the object is destroyed when the last call releases it". §2.3 stands.
- ADR-0008 "Not in this step": the typed object parameter of the peer is `Peer<T>` (this ADR); `NewDirectByteBuffer` over
  native memory is `NativeBuffer` (§9).

## Tests

| What                                                                                          | Where                    |
|-----------------------------------------------------------------------------------------------|--------------------------|
| unknown, closed and wrong-type handle; handles are not reused; `close` twice                  | `peer_table_test`        |
| the hook runs once, outside the lock, before the destructor; the destructor runs once, after the last call | `peer_table_test` |
| `close()` against a blocked call and against many callers, ThreadSanitizer clean              | `peer_table_test`        |
| `Peer<T>` as a parameter and a result of `native<&fn>`; a throwing constructor gives no handle; a closed handle is `IllegalStateException` | `peer_convert_test` (real JVM) |
| `Peer<T>` or `NativeBuffer` in a collection, optional, field or call into Java does not compile; `NativeBuffer` as a parameter | `compile_fail/` |
| `NativeBuffer`: same bytes in Kotlin, empty buffer, memory outlives its peer, memory released after the garbage collector takes the buffer | `peer_convert_test` (real JVM) |
| `NativePeer`: `close` idempotent, `IllegalStateException` after `close`, safety net through a fake `LeakGuard` | `android/src/test`       |
