# Type conversion by example

A learning guide to [ADR-0008](../adr/0008-type-conversion.md). How a native function takes a `String`, a `Boolean?`, an
`IntArray` or your own `data class` from Kotlin and gives one back, without writing a line of JNI, and what Kotlin sees
when something is wrong.

Every example has the Kotlin side and the C++ side. The compiler messages and exception texts marked `// output:` are
copied from the tests (`convert_test`, `convert_array_test`, `convert_collection_test`, `compile_fail/`).

## The rule in one picture

JNI knows `jstring`, `jint`, `jintArray`. Kotlin thinks `String`, `Int`, `IntArray`. A C++ library thinks `std::string`,
`int`, `std::vector<int>`. Someone has to translate, and it should not be you, in every function.

```
Kotlin                    jnirt does this                     your C++
------                    ---------------------------------   ------------------------------
String        ──────►     jstring -> UTF-16 -> UTF-8    ──►   const std::string &
Int?          ──────►     Integer -> null? -> value      ──►   std::optional<jint>
data class    ──────►     read the fields               ──►   struct Frame
                          (and back, for the result)
```

Kotlin analogy: Retrofit. You write `suspend fun getUser(): User`; a converter you never call turns JSON into `User`.
Here the same: you write `std::string open(JNIEnv*, jobject, const std::string &url)`; a *row* of one table,
`jnirt::Convert<std::string>`, does the translation. If you want to do it yourself there is `jnirt::fromJava<T>` and
`jnirt::toJava`, like `Json.decodeFromString<T>()`.

Everything below needs only:

```cpp
#include <jnirt/runtime/registry.h>        // it includes convert.h
using namespace jnirt;
```

## 1. A string, both ways

```kotlin
class Camera {
    external fun open(url: String): Boolean
    external fun describe(): String
}
```

```cpp
bool open(JNIEnv *env, jobject thiz, const std::string &url) {
    JNIRT_REQUIRE(url.rfind("rtsp://", 0) == 0, "url must start with rtsp://, got %s", url.c_str());
    return connect(url);                       // a plain C++ function from core/, knows nothing about JNI
}

std::string describe(JNIEnv *env, jobject thiz) {
    return "Камера 📷 #" + std::to_string(42);  // Cyrillic and emoji arrive in Kotlin as they are
}

r.add<CameraJ>().native<&open>("open").native<&describe>("describe");   // (Ljava/lang/String;)Z and ()Ljava/lang/String;
```

What jnirt does for you: checks that `url` is not null, reads it as UTF-16 and converts it to normal UTF-8, makes the
`Boolean` from `bool`, makes the result `String` from `std::string`, deletes every temporary reference.

What it replaced (`p4_demo`): `jstring` parameters, `JNIRT_REQUIRE(url != nullptr, ...)`, a `text(env, url)` helper over
`GetStringUTFChars` (which turns an emoji into six bytes), and `LocalRef<jstring> s(env, env->NewStringUTF(...)); return
s.release();`.

Text is exact. `"a\0b"` keeps its zero byte. An emoji is four bytes in `std::string` and a surrogate pair in Java. A
lone surrogate (a valid Java string, impossible in UTF-8) becomes `U+FFFD`. Invalid UTF-8 coming from C++ (a stray byte
from a camera name) becomes `U+FFFD` too and never throws.

If Java passes null:

```
// output:
java.lang.IllegalArgumentException: argument 1 is null, but this parameter is a non-null Ljava/lang/String;. Declare it as std::optional<...> if null is allowed.
    at com.acme.camera.Camera.open(Native Method)
```

`open` was not called at all. A null for a non-optional is a programmer error, so it is an `IllegalArgumentException`
(ADR-0002), and the stack trace names the Kotlin method.

## 2. `Boolean` and numbers

```kotlin
external fun setExposure(auto: Boolean, valueUs: Long, gain: Float): Int
```

```cpp
jint setExposure(JNIEnv *, jobject, bool autoMode, jlong valueUs, jfloat gain) {
    return sdk::setExposure(autoMode, valueUs, gain);
}
```

`bool` is a `Boolean` now. Numbers are the JNI types you already know (`jint` is `int32_t`, `jlong` is `int64_t`) and cost
nothing. What does **not** compile, on purpose:

```cpp
void setBuffer(JNIEnv *, jobject, size_t bytes);
// output:
// jnirt: Java has no unsigned numbers. Take jint / jlong and check the range yourself (size_t, uint32_t, uint64_t).
```

```cpp
void setBuffer(JNIEnv *, jobject, jlong bytes) {
    JNIRT_REQUIRE(bytes >= 0 && bytes <= kMaxBuffer, "bytes out of range: %lld", static_cast<long long>(bytes));
    sdk::setBuffer(static_cast<size_t>(bytes));
}
```

A Kotlin `enum` does not cross automatically: by ordinal it breaks when someone reorders constants. Pass a stable `Int`:

```kotlin
enum class Mode(val code: Int) { DAY(1), NIGHT(2) }
private external fun setMode(code: Int)
fun setMode(mode: Mode) = setMode(mode.code)
```

## 3. Nullable: `String?`, `Int?`

One rule: **`?` in Kotlin is `std::optional` in C++.** Nowhere else does null appear.

```kotlin
external fun connect(host: String, user: String?, port: Int?): String?
```

```cpp
std::optional<std::string> connect(JNIEnv *, jobject, const std::string &host,
                                   const std::optional<std::string> &user,      // String?
                                   std::optional<jint> port) {                  // Int?  (java.lang.Integer)
    int realPort = port.value_or(554);                                          // Kotlin: port ?: 554
    if (user) LOGI("login as %s", user->c_str());                               // Kotlin: user?.let { ... }
    auto session = sdk::connect(host, user.value_or("admin"), realPort);
    if (!session) return std::nullopt;                                          // Kotlin gets null
    return session->id();
}
```

| Kotlin           | C++                      |
|------------------|--------------------------|
| `x != null`      | `x.has_value()` / `if (x)` |
| `x!!`            | `x.value()` (throws) or `*x` (unchecked) |
| `x ?: default`   | `x.value_or(default)`    |
| `return null`    | `return std::nullopt;`   |

`Int?` is a real `java.lang.Integer`, so the call costs a method call into Java (`valueOf` / `intValue`). On a hot path
use a plain `Int` and a separate flag.

## 4. Calling Kotlin from C++

```kotlin
class Camera {
    fun onFrame(index: Int, label: String) { ... }
    fun currentProfile(): String = "hd"
    fun nickname(): String? = null
}
```

```cpp
struct CameraJ : JavaClass<CameraJ> {
    static constexpr const char *name = "com/acme/camera/Camera";

    static inline Method<CameraJ, void(jint, std::string)>      onFrame{"onFrame"};
    static inline Method<CameraJ, std::string()>                currentProfile{"currentProfile"};
    static inline Method<CameraJ, std::optional<std::string>()> nickname{"nickname"};
};

void process(JNIEnv *env, jobject camera) {
    CameraJ::onFrame(env, camera, 1, "кадр");                       // the Java string is made and deleted for you
    std::string profile = CameraJ::currentProfile(env, camera);     // a std::string, not a LocalRef
    std::optional<std::string> nick = CameraJ::nickname(env, camera);
}
```

If `currentProfile` is described as `std::string()` but Kotlin returns null:

```
// output:
java.lang.IllegalStateException: Camera.currentProfile() returned null; declare the result as std::optional<...>
```

That is a wrong description, not wrong data, so it is an `IllegalStateException`.

Fields work the same: `Field<CameraJ, std::string> name_{"name"}`, `name_.get(env, camera)`, `name_.set(env, camera, "Двор")`.
`Field<CameraJ, std::optional<std::string>>` can be set to `std::nullopt`.

## 5. By hand: `fromJava` and `toJava`

When the automatic layer does not fit: an old function that still takes a raw `jstring`, a value that comes out of an
object array, a loop that converts as it goes.

```cpp
std::string url = jnirt::fromJava<std::string>(env, urlParam);          // jstring -> std::string (null is an error)
auto maybe      = jnirt::fromJava<std::optional<std::string>>(env, s);  // null -> nullopt

LocalRef<jstring> java = jnirt::toJava(env, std::string("кадр"));       // always a LocalRef for objects
return java.release();                                                  // if you give it to Java
```

Raw types stay valid in the same function: `void f(JNIEnv*, jobject, jstring raw, const std::string &converted)`. Code
written in P3 and P4 does not change.

## 6. Arrays of primitives

```kotlin
external fun average(samples: FloatArray): Float
external fun histogram(): IntArray
external fun decrypt(data: ByteArray, key: ByteArray): ByteArray
```

```cpp
jfloat average(JNIEnv *, jobject, const std::vector<jfloat> &samples) {     // FloatArray, copied
    JNIRT_REQUIRE(!samples.empty(), "samples is empty");
    return std::accumulate(samples.begin(), samples.end(), 0.0f) / samples.size();
}

std::vector<jint> histogram(JNIEnv *, jobject) { return sdk::histogram(); } // IntArray

std::vector<std::byte> decrypt(JNIEnv *, jobject, const std::vector<std::byte> &data, const std::vector<std::byte> &key);
```

`std::vector<jint>` is an `IntArray`, **not** `List<Int>`: a `List<Int>` would box every element. On the border use
`IntArray` (`list.toIntArray()` in Kotlin).

**The `uint8_t` trap.** C++ libraries hold bytes as `uint8_t`, and `uint8_t` is the very same type as `jboolean`. So
`std::vector<uint8_t>` would silently be a `BooleanArray`, and fail at startup with "method not found". It does not compile:

```cpp
std::vector<uint8_t> decrypt(JNIEnv *, jobject, const std::vector<uint8_t> &data);
// output:
// jnirt: std::vector<uint8_t> is ambiguous (uint8_t is jboolean). ByteArray: std::vector<std::byte> (or jbyte); BooleanArray: std::vector<bool>
```

`std::byte` is a type of its own, the compiler can tell it apart. Converting for a library that wants `uint8_t *` is a cast
of a pointer: `reinterpret_cast<const uint8_t *>(data.data())`.

## 7. A piece of an array, into your own buffer

When you already have a buffer (an audio ring) or need only a slice, copy just that:

```kotlin
external fun pushAudio(pcm: ShortArray, offset: Int, count: Int)
external fun pullAudio(out: ShortArray): Int
```

```cpp
void pushAudio(JNIEnv *env, jobject, jshortArray pcm, jint offset, jint count) {
    jshort *slot = ring.reserve(count);
    jnirt::readArray(env, pcm, offset, slot, count);          // GetShortArrayRegion
    ring.commit(count);
}

jint pullAudio(JNIEnv *env, jobject, jshortArray out) {
    auto chunk = ring.take(jnirt::arrayLength(env, out));
    jnirt::writeArray(env, out, 0, chunk.data(), chunk.size());
    return static_cast<jint>(chunk.size());
}
```

The parameter stays a raw `jshortArray`: a copy into a `std::vector` would be the extra copy you are avoiding. A range
outside the array is the JVM's `ArrayIndexOutOfBoundsException`, passed to Kotlin as it is.

## 8. `withCritical`: a big array, no copy

For a large array and one short, fast pass. Kotlin analogy: `synchronized { }` where nothing may wait.

```cpp
jfloat peak(JNIEnv *env, jobject, jfloatArray samples) {
    jfloat result = 0;
    jnirt::withCriticalRead(env, samples, [&](const jfloat *p, std::size_t n) {   // read only: nothing is copied back
        for (std::size_t i = 0; i < n; ++i) result = std::max(result, std::fabs(p[i]));
    });
    return result;
}

void normalize(JNIEnv *env, jobject, jfloatArray samples, jfloat max) {
    jnirt::withCritical(env, samples, [&](jfloat *p, std::size_t n) {             // changes are visible in Kotlin
        for (std::size_t i = 0; i < n; ++i) p[i] /= max;
    });
}
```

Inside the lambda:

- there is no `env`, so there is no JNI by accident; in debug builds `jnirt::env()` and class_cache calls assert;
- nothing may wait (mutex, network, `sleep`): the garbage collector may be held while you are in here;
- `LOGI` is fine: inside `withCritical` a log line goes to logcat only, because sending it to Kotlin is a JNI call;
- an exception is fine: the array is released and the exception arrives as always.

It is released at the closing brace, like `use { }`. There is no way to forget it, and no `Get<Type>ArrayElements` with
its three release modes.

## 9. A direct `ByteBuffer`: camera frames

For large data that crosses all the time. Kotlin allocates once, native sees the same memory.

```kotlin
class FrameReader(width: Int, height: Int) {
    private val buffer = ByteBuffer.allocateDirect(width * height * 3 / 2)     // NV21, once
    external fun readFrame(target: ByteBuffer): Int                             // fills it, returns the size
}
```

```cpp
jint readFrame(JNIEnv *, jobject, jnirt::DirectBuffer target) {                 // pointer + size, no copy
    return static_cast<jint>(sdk::grabFrame(target.data(), target.size()));
}
```

```
// output, for ByteBuffer.allocate(100) (not direct):
java.lang.IllegalArgumentException: argument 1 is not a direct ByteBuffer
```

`size()` is the capacity. `position` and `limit` are not read; pass a slice as offset and length. Handing Kotlin a
`ByteBuffer` over *native* memory is not in P5 (who keeps the memory alive? that is P6).

## 10. `List<String>` and the rest of the collections

```kotlin
external fun setAllowedHosts(hosts: List<String>)
external fun discovered(): List<String>
```

```cpp
void setAllowedHosts(JNIEnv *, jobject, const std::vector<std::string> &hosts) { sdk::setAllowedHosts(hosts); }
std::vector<std::string> discovered(JNIEnv *, jobject) { return sdk::scan(); }       // an ArrayList
```

```
// output, for listOf("a", null) passed from Java:
java.lang.IllegalArgumentException: argument 1: element 1 is null, but the element type is a non-null Ljava/lang/String;
```

For `List<String?>` write `std::vector<std::optional<std::string>>`. A list of 100 000 strings does not overflow the table
of local references: each element's reference lives for one iteration of the loop.

`Map<String, String>` is `std::map<std::string, std::string>` (or `unordered_map`). It is slow by nature (three calls into
Java per pair); for a hot path pass two arrays. Kotlin sees the entries in the order of the C++ container.

## 11. Your own type: `data class Frame`

```kotlin
data class Frame(@JvmField val width: Int, @JvmField val height: Int, @JvmField val label: String)
```

Three places in C++. The plain struct, in `core/` (no `jni.h`):

```cpp
struct Frame { int32_t width; int32_t height; std::string label; };
```

The description of the Kotlin class (as in P3) and one row for the table, both in `bindings/`:

```cpp
struct FrameJ : JavaClass<FrameJ> {
    static constexpr const char *name = "com/acme/camera/Frame";
    static inline Field<FrameJ, jint>        width{"width"};
    static inline Field<FrameJ, jint>        height{"height"};
    static inline Field<FrameJ, std::string> label{"label"};
    static inline Constructor<FrameJ, void(jint, jint, std::string)> create;
};

template<> struct jnirt::Convert<Frame> : jnirt::ConvertObject<Frame, FrameJ> {
    static Frame fromJava(JNIEnv *env, jobject f) {
        return {FrameJ::width.get(env, f), FrameJ::height.get(env, f), FrameJ::label.get(env, f)};
    }
    static LocalRef<jobject> toJava(JNIEnv *env, const Frame &f) {
        return FrameJ::create(env, f.width, f.height, f.label);
    }
};
```

Kotlin analogy: your own `KSerializer<Frame>`. Now `Frame` is as good as `std::string` everywhere:

```kotlin
external fun lastFrame(): Frame
external fun find(label: String): Frame?
external fun history(limit: Int): List<Frame>
external fun groups(): Map<String, List<Frame>>
```

```cpp
Frame                                      lastFrame(JNIEnv *, jobject);
std::optional<Frame>                       find(JNIEnv *, jobject, const std::string &label);
std::vector<Frame>                         history(JNIEnv *, jobject, jint limit);
std::map<std::string, std::vector<Frame>>  groups(JNIEnv *, jobject);
```

No translation in those functions. A type with no row does not compile:

```cpp
std::vector<Settings> all(JNIEnv *, jobject);
// output:
// jnirt: this C++ type has no JVM descriptor. Supported: ... Add a row for it:
//   template<> struct jnirt::Convert<Settings> : jnirt::ConvertObject<Settings, SettingsJ> { ... }
```

A type that crosses as a number (a `std::chrono::milliseconds` as `jlong`) needs the same members without the base class.

## 12. An array of objects

When Kotlin takes `Array<Frame>`, or you want to walk a large one yourself without a copy of everything:

```kotlin
external fun countWide(frames: Array<Frame>): Int
```

```cpp
jint countWide(JNIEnv *env, jobject, jnirt::Array<FrameJ> frames) {         // the signature is ([Lcom/acme/camera/Frame;)I
    jint count = 0;
    for (jsize i = 0, n = frames.size(env); i < n; ++i) {
        LocalRef<jobject> f = frames.get(env, i);                          // deleted on each iteration
        if (FrameJ::width.get(env, f.get()) > 1920) ++count;
    }
    return count;
}
```

`jnirt::Array<C>` is a view of a `jobjectArray` with its element class in the type; nothing is converted. `jnirt::newArray<C>(env, n)`
makes one.

## 13. What `init` says about the Kotlin side

All of these compile in Kotlin and in C++ and fail at startup, with "method not found". `init()` now says why:

```kotlin
@JvmInline value class CameraId(val raw: Long)
external fun open(id: CameraId): Boolean            // the JVM name is open-Ab3xQ9z
```

```
// output:
E jnirt: method com.acme.camera.Camera.open (J)Z: not found
         hint: found open-Ab3xQ9z(J)Z. A value class parameter (or UInt) changes the method name. Take Long on the border.
```

Fix: `private external fun open(id: Long): Boolean` and `fun open(id: CameraId) = open(id.raw)`.

| Kotlin                                  | What `init` says                                                                     |
|-----------------------------------------|--------------------------------------------------------------------------------------|
| `internal external fun reset()`         | found `reset$camera_release()V`: `internal` adds the module name; use `public` or `private` |
| `external fun version(): Int` in `companion object` | found in `Camera$Companion`: add `@JvmStatic`                             |
| `suspend`, `Flow`                       | not on the border; use a callback (P7)                                               |

## 14. What it costs

Conversion hides calls into Java. The numbers on a device are in `examples/p5_bench`; the shape is:

| Type                          | Cost beyond the copy                                    |
|-------------------------------|---------------------------------------------------------|
| numbers, `bool`               | none                                                    |
| `std::string`                 | one copy and a UTF conversion (ASCII is a byte copy)    |
| `std::optional<jint>`         | one call into Java                                      |
| `std::vector<jint>`           | one copy                                                |
| `withCritical`, `DirectBuffer`| none                                                    |
| `std::vector<T>` as `List`    | about one call into Java per element                    |
| `std::map<K, V>`              | about three calls per pair                              |
| your own `Frame`              | one call per field                                      |

The rule is still coarse-grained calls: one call that carries a `List<Frame>` is cheaper than a thousand calls that
carry one `Frame` each.

## 15. What does not compile

| You wrote                                       | Message (the point)                                                  | Instead                           |
|-------------------------------------------------|----------------------------------------------------------------------|-----------------------------------|
| `size_t`, `uint32_t`, `uint64_t` parameter      | Java has no unsigned numbers                                         | `jlong` and check the range       |
| `std::vector<uint8_t>`                          | `uint8_t` is `jboolean`; use `std::vector<std::byte>`                | `std::vector<std::byte>`          |
| `std::vector<Settings>` with no row             | no row for `Settings`; add `jnirt::Convert<Settings>`                | write the row (§11)               |
| `std::vector<jstring>`                          | raw references as elements would dangle                              | `std::vector<std::string>`        |
| `std::optional<jstring>`                        | `jstring` is already nullable                                        | `jstring`, or `std::optional<std::string>` |
| `std::string_view` as a result of a Java call   | it would point to freed memory                                       | `std::string`                     |
| `jobject` parameter with no Java type           | which Java class? (as in P3)                                         | `native<&f, void(ListenerJ)>`     |
| `std::vector<jint>` when Kotlin has `List<Int>` | compiles; startup says not found                                     | `IntArray` in Kotlin              |
