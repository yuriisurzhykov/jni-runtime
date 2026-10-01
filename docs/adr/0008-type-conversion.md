# ADR-0008 - Type conversion

## Status

Accepted. Implemented in roadmap P5. Explained with examples:
[learning/type-conversion-by-example.md](../learning/type-conversion-by-example.md). Builds on the signatures of
[ADR-0005](0005-signatures.md), the class cache of [ADR-0006](0006-class-cache-and-registry.md) and the `entry()` of
[ADR-0007](0007-exceptions-entry-and-log-sink.md); finishes ADR-0001 §7.

## Context

After P4 a native function still speaks raw JNI: `jstring`, `jboolean`, `jintArray`. Every library writes its own
`text(env, jstring)` helper, almost always with `GetStringUTFChars`, which is *modified* UTF-8 (an emoji arrives as six
bytes, `U+0000` as two). Nulls are checked by hand or not at all. Kotlin thinks `String`, `Boolean`, `IntArray`; the C++
library thinks `std::string`, `bool`, `std::vector<int>`; a person translates between them in every function.

ADR-0005 left three things for this step: `bool` (banned until it can be converted), `jobjectArray` (needs the element
type) and the C++ types `std::string` / collections / `ByteBuffer`. ADR-0007 left a public UTF-8 to UTF-16 conversion.

Goal in one sentence: **a native function is written with ordinary C++ types, and jnirt converts at the border: the same
way every time, checked, and without leaking local references.**

## Decision

### 1. One table, two layers

There is one table, `jnirt::Convert<T>`: for a C++ type `T` it says which JNI type crosses the border, which Java type
that is, and how to convert each way. (Kotlin analogy: a `KSerializer<T>` per type.) Two layers use it:

1. **Explicit functions**, for the cases the automatic layer does not cover:
   `jnirt::fromJava<std::string>(env, jstr)` and `jnirt::toJava(env, value)`.
2. **Automatic**, which calls the same rows:
   - the parameters and the result of every native registered with `native<&fn>` (inside the `try` of `entry()`, so a
     failed conversion becomes a Java exception like any other error);
   - the arguments and results of `Method`, `StaticMethod`, `Constructor`, `Field`, `StaticField` of class_cache.

`signature<F>` is built from the same table, so the Java signature of a function with C++ types is derived, not written
(ADR-0001 §7.2): `signature<bool(const std::string &)>` is `(Ljava/lang/String;)Z`.

Raw JNI types (`jint`, `jstring`, `jintArray`, a described class) are rows of the same table that convert nothing and
cost nothing. Code written for P3 / P4 compiles and behaves as before.

**One C++ type means one Java type**, so the signature follows from the C++ function and there is no second place to
write it wrong. Rejected: describe the Java type next to every parameter (`native<&fn, void(List<String>)>`); it is more
flexible (one C++ type could go to `List` or `Array`) but doubles the description of every function and brings back
"described one thing, wrote another". The explicit Java signature of ADR-0006 §5 stays for the one case that needs it: a
`jobject` that must say which Java class it is.

A row is a specialization of `jnirt::Convert`, like the descriptor table of ADR-0005. A type without a row does not
compile, with a message that lists what is supported. A library adds rows for its own types (§7).

### 2. The table

| C++                                             | Kotlin / Java                      | Direction    | Notes                                           |
|-------------------------------------------------|------------------------------------|--------------|-------------------------------------------------|
| `jint`, `jlong`, `jfloat`, `jdouble`, `jbyte`, `jshort`, `jchar`, `jboolean` | `Int`, `Long`, ... | both | as before, no conversion                         |
| `bool`                                          | `Boolean`                          | both         | allowed now; `jboolean` stays                    |
| `std::string` (UTF-8)                           | `String`                           | both         | §3                                              |
| `std::u16string`                                | `String`                           | both         | UTF-16 as it is                                  |
| `std::string_view`                              | `String`                           | to Java only | an argument of a Java call; not a result         |
| `std::optional<T>`                              | `T?` (`Int?` is `Integer`)         | both         | §4                                              |
| `std::vector<jint>` ... (8 element types)       | `IntArray` ...                     | both         | §5; copy                                         |
| `jnirt::DirectBuffer`                           | `java.nio.ByteBuffer` (direct)     | from Java only | §6                                             |
| `std::vector<T>`, `T` not primitive             | `List<T>`                          | both         | §7                                              |
| `std::map<K, V>`, `std::unordered_map<K, V>`   | `Map<K, V>`                        | both         | §7                                              |
| `jnirt::Array<C>`                               | `Array<C>` (`jobjectArray`)        | both         | §7; holds the reference, no element is converted |
| your `struct Frame`                             | your `data class Frame`            | both         | a row you write, §7                              |
| raw `jstring`, `jclass`, `jthrowable`, `j...Array`, a described class | as before        | raw          | stay valid, no conversion                      |

Not converted on purpose:

- **Unsigned types** (`unsigned`, `uint32_t`, `uint64_t`, `size_t`). Java has none, Kotlin's `UInt` is a value class
  (§9), and a silent narrowing of `size_t` to `jint` is a classic bug. Compile error:
  `Java has no unsigned numbers: take jint / jlong and check the range yourself`.
  (`uint8_t` and `uint16_t` are the same types as `jboolean` and `jchar`, see ADR-0005 §5.)
- **`enum class` to Kotlin `enum`.** By ordinal it breaks when constants are reordered, by name it is slow. Pass a stable
  `Int` code, as error domains do (ADR-0002).
- **`const char *`.** Ambiguous (nullable, owning?). Use `std::string`.

### 3. Strings

- **Java to C++:** `GetStringLength` + `GetStringRegion` into a buffer (on the stack up to 256 characters, else on the
  heap), then UTF-16 to UTF-8. This is UTF-16 as ADR-0001 §7.1 requires, but with `GetStringRegion` instead of
  `GetStringChars`: it only copies, so there is no `ReleaseStringChars` to pair, and nothing is pinned.
  An unpaired surrogate (a valid Java string, impossible in UTF-8) becomes `U+FFFD`. `U+0000` stays a zero byte.
  A string of only ASCII is copied byte by byte.
- **C++ to Java:** `utf8ToUtf16` + `NewString` of ADR-0007 §4, now public through `toJava`. Invalid bytes become
  `U+FFFD`, one per bad sequence. Nothing throws on bad text.
- `GetStringUTFChars` / `NewStringUTF` are not used anywhere in jnirt.

### 4. Null, `bool`, boxing

**`?` in Kotlin is `std::optional` in C++, and nowhere else does null appear.**

| Kotlin    | C++                          | A null from Java                          |
|-----------|------------------------------|-------------------------------------------|
| `String`  | `std::string`                | the function is not called; see below     |
| `String?` | `std::optional<std::string>` | `std::nullopt`                            |
| `Int?`    | `std::optional<jint>`        | `std::nullopt`                            |

- **A null for a non-optional parameter is `IllegalArgumentException`** (a programmer error, ADR-0002), thrown before
  the native function runs: `argument 2 is null, but this parameter is a non-null Ljava/lang/String;. Declare it as
  std::optional<...> if null is allowed.` The stack trace already names the Kotlin method (the exception is thrown from
  the native frame), so the message carries the argument number and the type. Rejected: `NullPointerException` as Kotlin
  does; one more exception type in the table of ADR-0002 §5 for no gain.
- **A null result of a Java call that the C++ type does not allow is `IllegalStateException`:**
  `Camera.currentProfile() returned null; declare the result as std::optional<...>`. The description is wrong, not the
  data.
- Raw references (`jstring`, `jobject`, `LocalRef`) are not checked: there the author decides.
- `std::optional<jint>` and the other primitives are `Integer` and so on (`valueOf` / `intValue`): the classes are found
  by `init()` with the other classes of the runtime. Every such parameter is a call into Java; on a hot path prefer a
  plain `Int`.
- `std::optional<jstring>` (an optional raw reference) does not compile: `jstring` is already nullable.
- `bool` is `jboolean` on the JNI side; the message `use jboolean, not bool` of ADR-0005 is gone.

### 5. Arrays of primitives and large data

Four ways, from simple to fast:

| Way            | C++                                                          | Use when                                     |
|----------------|--------------------------------------------------------------|----------------------------------------------|
| copy whole     | `std::vector<jint>` ↔ `IntArray`                              | small arrays; the default                    |
| copy a piece   | `jnirt::readArray(env, arr, start, dst, n)` / `writeArray`    | your own buffer exists, or only a slice      |
| no copy        | `jnirt::withCritical(env, arr, [](jint *p, size_t n) {...})`  | one short, fast pass over a large array      |
| direct buffer  | `jnirt::DirectBuffer`                                         | large data that crosses all the time         |

- The whole-array and piece operations are `Get<Type>ArrayRegion` / `Set<Type>ArrayRegion`: copies, no paired call.
  A bad range is the `ArrayIndexOutOfBoundsException` of the JVM, passed on as a `JavaException`.
- **`Get<Type>ArrayElements` is not offered.** On ART it is often a copy, and it needs three release modes. A copy, a
  piece, `withCritical` and a direct buffer cover every use with fewer things to get wrong.
- **`withCritical` only takes a lambda** (like `use { }`): the border is visible and the array is released even if the
  lambda throws. The lambda gets a pointer and a length and **no `env`**, so JNI is not called by accident. Inside, no
  JNI and no blocking (the garbage collector may be held). Debug builds assert if `jnirt::env()` or a call of class_cache
  is made inside. `withCriticalRead` (read only, no copy back) exists for the common read-only pass.
  The lambda may return a value. If it throws, the array is released with what was written so far, and the exception
  goes on. `withCritical` does not nest: the second array would need `GetArrayLength` inside the first region, which JNI
  does not allow (`-Xcheck:jni` warns). Debug builds assert; copy a piece with `readArray` instead.
- **Logging inside `withCritical`:** the log forwarder of ADR-0007 §7 calls Java, which is forbidden there. A thread flag
  set by `withCritical` makes the forwarder decline, and the line goes to logcat. It is a fifth case of ADR-0007 §7.
- **A buffer for a handle.** `readArray` / `writeArray` take the array handle, so the handle says what the Java array
  is: a `jbyteArray` accepts a `jbyte`, `std::byte` or `uint8_t` buffer, a `jbooleanArray` a `jboolean` or `bool` one, a
  `jcharArray` a `jchar` or `char16_t` one; any other mismatch (a `jlong` buffer for a `jintArray`) does not compile.
  The ambiguity below is only about `std::vector<uint8_t>`, which has no handle.
- **Bytes.** `uint8_t` is the same type as `jboolean`, so `std::vector<uint8_t>` would silently be a `BooleanArray`.
  It does not compile: `uint8_t is jboolean; ByteArray is std::vector<std::byte> (or jbyte), BooleanArray is
  std::vector<bool>`.

  | C++                      | Kotlin         |
  |--------------------------|----------------|
  | `std::vector<std::byte>` | `ByteArray`    |
  | `std::vector<jbyte>`     | `ByteArray`    |
  | `std::vector<bool>`      | `BooleanArray` |
  | `std::vector<jchar>` `jshort` `jint` `jlong` `jfloat` `jdouble` | `CharArray` `ShortArray` `IntArray` `LongArray` `FloatArray` `DoubleArray` |

### 6. Direct `ByteBuffer`

Kotlin allocates `ByteBuffer.allocateDirect(n)` once and passes it many times; native sees the same memory
(ADR-0001 §7.3), with no copy and no pinning. `jnirt::DirectBuffer` is a pointer and a size; it does not own the memory.

- A buffer that is not direct is `IllegalArgumentException("... buffer is not direct")`.
- `size()` is the **capacity**. `position` and `limit` are not read (that would be two calls into Java); a slice is passed
  as offset and length parameters.
- Only from Java to C++. `NewDirectByteBuffer` over memory that native code owns is an ownership question (Kotlin could
  keep the buffer longer than the C++ memory lives): P6.
- **Only as a native parameter.** The memory belongs to the Java object, and the object is alive during the native call.
  The result of a call into Java (or a field) can be a buffer that nothing else holds: the local reference of the result
  is deleted when the call returns, the buffer may be collected, and the pointer dangles. So `Method<C, DirectBuffer()>`
  and `Field<C, DirectBuffer>` do not compile (the row says `borrowsFromJava`, §2). To use such a buffer, keep the
  reference and ask by hand: `jnirt::fromJava<DirectBuffer>(env, ref.get())`; `ref` must outlive the pointer.
- `data()` is a `std::byte *`; `as<T>()` gives the memory as `T *` (`uint8_t` pixels, `float` samples).

### 7. Objects, collections and your own types

- **`std::vector<T>` for a non-primitive `T` is `java.util.List`** (not `Array`: Kotlin APIs take `List`). Reading uses
  `toArray()` once, then the elements, so a `LinkedList` is not quadratic. Writing makes an `ArrayList`. A
  `std::vector<jint>` is an `IntArray`, **not** `List<Int>`: a `List<Int>` boxes every element. Use `IntArray` on the
  border.
- **`std::map` / `std::unordered_map` are `java.util.Map`.** Reading is `entrySet().toArray()` and two calls per pair;
  writing makes a `LinkedHashMap`, so Kotlin sees the C++ iteration order. This is slow by nature (it conflicts with
  "coarse calls"); the documentation says so, and a hot path passes two arrays instead.
- **A local reference per element lives for one iteration** (`LocalRef` in the loop body), so the number of local
  references does not depend on the length of the list. This is an exit criterion of the phase.
- **A raw reference as an element** (`std::vector<jstring>`) does not compile: it would point to a local reference
  that is already deleted. Elements are values (`std::string`, your own type). The same holds for a `DirectBuffer` (§6)
  and for the keys and values of a `Map`.
- **A number in a `Map`** (`std::map<std::string, jint>`) is boxed, because a `Map` can hold nothing else (`Map<String,
  Int>`). That is the only place where a number crosses as an object besides `std::optional<jint>`; both use one helper.
- **Elements and values follow the same rules as parameters**: a null element for a non-optional type is an
  `IllegalArgumentException` naming the place (`element 3`, `the key of entry 3`, `the value of entry 3`);
  `std::vector<std::optional<std::string>>` is `List<String?>`. When Java hands such a value to a native, Java gets the
  exception; when C++ reads it from the result of its own call into Java, the C++ caller gets the `IllegalArgument`.
- **`jnirt::Array<C>`** is the Java `Array<C>` (`jobjectArray`) with its element class `C` (a described class) in the
  type, so the signature is `[Lcom/acme/camera/Frame;`. It holds a local reference to the array (a second one made with
  `NewLocalRef`, so it survives the reference JNI gave), converts no element, and is as thread-bound as `LocalRef`:
  `size(env)`, `get(env, i)` (a `LocalRef<jobject>`, null for a null element), `set(env, i, object)`, `raw()` and
  `newArray<C>(env, n)`. A bad index is the JVM's exception, thrown as `JavaException`. A null array is an error unless
  `std::optional<Array<C>>`. It closes the `jobjectArray` item of ADR-0005 §9. A `String[]` is `Array<StringJ>` with
  `StringJ` described as `java/lang/String`; for a list of texts use `std::vector<std::string>`.
- **Your own types.** A library adds one row for each of its structs, in `bindings/`; `core/` stays free of `jni.h`:

  ```cpp
  template<> struct jnirt::Convert<Frame> : jnirt::ConvertObject<Frame, FrameJ> {
      static Frame fromJava(JNIEnv *env, jobject f);                    // read the fields
      static LocalRef<jobject> toJava(JNIEnv *env, const Frame &f);     // FrameJ::create(...)
  };
  ```

  After that `Frame`, `std::optional<Frame>`, `std::vector<Frame>` (a `List`) and `std::map<std::string, Frame>` work
  everywhere. The row is found from the C++ type; a member inside `FrameJ` could not be (the function has `Frame`, not
  `FrameJ`). A row for a type that crosses as a primitive (a `std::chrono::milliseconds` as `jlong`) needs only the same
  members, without the base class.

### 8. What the machinery does

- **Native parameters.** `native<&fn>` registers a function with the JNI types (`jstring`, `jboolean`) that converts
  every parameter, calls `fn` with C++ types and converts the result, all inside the `try` of ADR-0007. A parameter may be
  written `T`, `const T &` or `T &&`. A converted value lives until `fn` returns, so a reference parameter is safe.
  Order is not specified: if two arguments are bad, either may be reported.
- **Explicit Java type** (`native<&fn, void(ListenerJ)>`, ADR-0006 §5) is only for a `jobject`. Its check is
  now: for each parameter, the C++ type of `fn` is the value type of the Java one (`jobject` for a described class, the
  same type for everything else).
- **Calls into Java.** `Method<C, R(A...)>` takes `A` as `const A &` (by value for numbers and pointers), converts, calls,
  checks for a pending exception (ADR-0006 §4) and converts the result. A result that is a raw reference or a described
  class is still a `LocalRef`; any other result is the C++ value.
- **Order of cleanup.** Temporary Java objects (the `jstring` made from a `std::string` argument) are deleted at the end
  of the call, also when it throws.

### 9. Kotlin shape rules, and a hint at startup

All of these compile in Kotlin and C++, and fail at startup with "method not found", which does not say why. They are
rules for the border, and a hint for the log:

| Kotlin                                  | JVM sees                               | Rule                                              |
|-----------------------------------------|----------------------------------------|---------------------------------------------------|
| `value class` parameter, `UInt`, `ULong`| name is mangled: `open-Ab3xQ9z`        | do not use on the border; pass `Long`, wrap in Kotlin |
| `internal fun`                          | `open$module_name`                     | `public` or `private` on the border               |
| function in `companion object`          | method of `Camera$Companion`, not static | `@JvmStatic`                                    |
| `suspend fun`, `Flow`                   | extra `Continuation` parameter, or a type | not on the border; callbacks (P7)             |
| default parameter values                | one method with all parameters         | describe the full signature                       |

When a method or native is not found, `init()` looks (only then) for a method of the same class with a mangled name
(`name-...`, `name$...`, and `name$default` for default parameter values) and for the same method in
`Class$Companion`, and adds a line to the problem. The methods are listed with `Class.getDeclaredMethods()`, which is
reflection, so it runs only when a lookup has failed. Example:

```
method com.acme.Camera.open (J)Z: not found
  hint: the class has `open-Ab3xQ9z`: Kotlin gives a function with a value class (or UInt, ULong...) in its signature a
  mangled name. Take the underlying type (Int, Long) on the border and wrap it in Kotlin.
```

The hints are for methods and natives; fields are not looked at. The cost is paid only on a startup error.

### 10. Cost of crossing

Conversion hides calls into Java: `std::optional<jint>` is `Integer.valueOf`, a `List` is one call per element, a
`Map` is three per pair. The learning guide has a table of costs. Host micro-benchmarks (`tests/host`, not run by
`ctest`) print the numbers, and `examples/p5_bench` runs the same cases on a device (Edge II) for the real ones. The rule
of the roadmap stays: coarse calls.

## Consequences

- A native function and a Java call are written with ordinary C++ types; no `text()` helper, no `GetStringUTFChars`, no
  null check by hand, no `LocalRef` for a temporary string.
- A mistake that compiled and failed at startup now often fails to compile (`uint8_t` vector, unsigned, raw reference
  element, optional raw reference).
- `signature.h` is built on the new table; the public `descriptor<T>` and `signature<F>` keep their names and values.
  `jnirt::JavaClass` stays declared there for its users.
- `init()` finds a few more classes of the JDK (`Integer` and the other boxes, `Collection`, `List`, `Map`,
  `Map.Entry`, `ArrayList`, `LinkedHashMap`). They are in the JDK of every Android version, and `init()` names the class
  if one is missing.

## Changes to earlier documents

- ADR-0001 §7.1: "`GetStringChars`" becomes "UTF-16 (`GetStringRegion`)". The rule is the same.
- ADR-0005 §5 and §9: `bool` is allowed; `jobjectArray`, `std::string`, `ByteBuffer` and collections are done here.
- ADR-0004 §9: the non-owning view for parameters is the converted value; raw `jobject` parameters stay for the explicit
  Java signature.
- ADR-0007 §7: a fifth reason for the forwarder to decline (inside `withCritical`).
- ADR-0007 §2: a `IllegalArgument` raised by a conversion has no `(file:line)`: the place would be a line of jnirt.

## Not in this step

| What                                                         | When / why                                                                         |
|--------------------------------------------------------------|------------------------------------------------------------------------------------|
| `NewDirectByteBuffer` over native memory, ownership of buffers | P6: who keeps the memory alive                                                   |
| typed object parameter instead of `jobject` + explicit Java type (`Param<ListenerJ>`) | P6 / P7 with listeners and peers                                  |
| `suspend`, `Flow`, lambdas as `std::function`                | P7                                                                                 |
| `enum class` conversion, `Set`, `std::chrono`, `std::variant` | no real need yet; a row can be added by the library, `Set` is the same mechanism as `List` |
| `Get<Type>ArrayElements`, `GetStringChars`                   | deliberately not offered (§3, §5)                                                  |
| codegen of rows                                              | P11                                                                                |

## Tests

| What                                                                                   | Where                                     |
|----------------------------------------------------------------------------------------|-------------------------------------------|
| every row: descriptors and signatures at compile time                                  | `signature_test`, `convert_test`          |
| strings: ASCII, Cyrillic, emoji, `U+0000`, unpaired surrogate, empty, 1 MB, both ways   | `convert_test`                            |
| null: non-optional parameter, optional, result, element, key                            | `convert_test`                            |
| round trip of every type through a real native method and a real Java call (both layers) | `convert_test`                          |
| arrays: copy, piece, bad range, `withCritical` (changes, exception inside, logs go to logcat) | `convert_array_test`                |
| direct buffer: direct, not direct, null, capacity                                      | `convert_array_test`                      |
| `List`, `Map`, `Frame`, `Array<Frame>`: 100 000 elements, local references stay flat    | `convert_collection_test`                 |
| what must not compile (unsigned, `uint8_t` vector, raw element, optional raw, no row)  | `compile_fail/`: one `ctest` test per file; the compiler runs with `-fsyntax-only` and the output must contain the `// EXPECT:` text (not `try_compile`: a test shows each case on its own, and configure stays fast) |
| startup hints: companion, `$` and `-` names, `$default`                                 | `kotlin_hints_test` (the matcher without a JVM, the startup against `KotlinShapes.java`) |
| cost of crossing (printed, no thresholds)                                              | `convert_bench` (not in `ctest`)          |
