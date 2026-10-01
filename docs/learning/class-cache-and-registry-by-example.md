# Class cache and registry by example

A learning guide to [ADR-0006](../adr/0006-class-cache-and-registry.md). How a library says "my native code reads this
Kotlin field, calls that Kotlin method and implements those `external fun`", and how jnirt turns that into one startup
that either works or tells you every mistake at once.

Everything here exists: `class_cache.h`, `registry.h`, `exception.h`.

## The idea in one picture

```
Kotlin class Camera                      C++ description (you write it once)
-----------------------                  --------------------------------------------------
var nativeHandle: Long          <----    Field<CameraJ, jlong>            nativeHandle{"nativeHandle"}
fun onFrame(i: Int, s: String)  <----    Method<CameraJ, void(jint, jstring)>   onFrame{"onFrame"}
external fun nativeOpen(...)    ---->    r.add<CameraJ>().native<&nativeOpen>("nativeOpen")
```

Arrows to the left: native code **calls** Kotlin. Arrow to the right: Kotlin **calls** native code. Both are checked
at startup, in `JNI_OnLoad`, and both are named exactly in the log if they do not match.

Kotlin analogy: the left column is a Retrofit interface (you describe what exists, the library finds and calls it);
the right column is Dagger/Hilt registration (you list what you provide, the framework checks the graph when the app
starts, not at the first use).

## 1. Describe the class

```cpp
#include <jnirt/runtime/class_cache.h>

struct CameraJ : jnirt::JavaClass<CameraJ> {
    static constexpr const char* name = "com/acme/camera/Camera";

    static inline jnirt::Field<CameraJ, jlong>                 nativeHandle{"nativeHandle"};
    static inline jnirt::Method<CameraJ, void(jint, jstring)>  onFrame{"onFrame"};
    static inline jnirt::StaticMethod<CameraJ, jint(jint)>     maxFrames{"maxFrames"};
    static inline jnirt::Constructor<CameraJ, void(jstring)>   create;
};
```

- `name` is the JVM name: slashes, and `$` for a nested class (`"com/acme/camera/Camera$State"`). A dot or `L...;` does
  not compile.
- The types in `void(jint, jstring)` are the ones from [signatures-by-example](signatures-by-example.md). The signature
  string `(ILjava/lang/String;)V` is built for you.
- Writing a member is the whole registration of it. There is no list to update (§5 explains how).

| You write                        | It is, in Kotlin                                         |
|----------------------------------|----------------------------------------------------------|
| `Field<C, T>`                    | a property with a backing field                          |
| `StaticField<C, T>`              | `@JvmField` / `const val` in a companion                 |
| `Method<C, R(A...)>`             | an instance function                                     |
| `StaticMethod<C, R(A...)>`       | an `@JvmStatic` function                                 |
| `Constructor<C, void(A...)>`     | a constructor                                            |

Kotlin names are not always the JVM names (ADR-0001): `internal fun foo()` is `foo$module` in bytecode, a function with
a value class parameter is mangled, a companion function without `@JvmStatic` lives on `Camera$Companion`. Everything
that native code touches is `public` or `private`, has no value classes in its signature, and static things are
`@JvmStatic`. If you break this, startup tells you which member was not found.

## 2. Call it

```cpp
jlong handle = CameraJ::nativeHandle.get(env, camera);
CameraJ::nativeHandle.set(env, camera, 0);

auto name = jnirt::LocalRef<jstring>(env, env->NewStringUTF("front"));
CameraJ::onFrame(env, camera, 1, name.get());           // a call looks like a call
jint max = CameraJ::maxFrames(env, 4);                  // static: no object
auto created = CameraJ::create(env, name.get());        // LocalRef<jobject>
```

- `env` first, always: you already have it, and a hidden lookup on every call would cost.
- Arguments are JNI values (`name.get()`). Results are `void`, a primitive, or a **`LocalRef`** for an object, so
  nothing is ever deleted by hand.
- A parameter or result that is one of your classes is written as its description (`ListenerJ`) and passed as a
  `jobject`. A class may name itself: `Method<CameraJ, CameraJ()>`.
- In debug builds the object is checked with `IsInstanceOf`. Passing a `jstring` where a `Camera` is expected stops
  with "the object is not an instance of com.acme.camera.Camera" instead of a crash inside the JVM later.

### When Java throws

```cpp
try {
    CameraJ::onFrame(env, camera, 1, name.get());
} catch (const jnirt::JavaException& e) {
    LOGE("onFrame failed: %s", e.what());               // java.lang.IllegalStateException: no surface
}
```

The pending Java exception is already cleared when you catch this, so the next JNI call is legal. If you do not catch
it, `entry()` ([ADR-0007](../adr/0007-exceptions-entry-and-log-sink.md)) hands the original `Throwable` (`e.throwable()`) back to Kotlin, so a Kotlin exception that went
through native code arrives as the same object. On your own native thread nobody is above you: catch it where you know
the context.

Kotlin analogy: `JavaException` is the exception you would get calling a Java method from Kotlin, only here the
conversion is explicit, because JNI does not throw by itself: it only sets a flag.

## 3. Register the natives

```cpp
jboolean nativeOpen(JNIEnv* env, jobject thiz, jstring url);        // instance: second parameter jobject
jint     nativeCount(JNIEnv* env, jclass cls);                      // static: second parameter jclass
void     nativeSubscribe(JNIEnv* env, jobject thiz, jobject listener);

void register_camera(jnirt::Registry& r) {
    r.add<CameraJ>()
     .native<&nativeOpen>("nativeOpen")
     .native<&nativeCount>("nativeCount")
     .native<&nativeSubscribe, void(ListenerJ)>("nativeSubscribe");
    r.add<ListenerJ>();                 // ListenerJ has members (we call it back) but no natives
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    return jnirt::init(vm, {register_camera});
}
```

- The Java signature is the one of the function **without** `JNIEnv*` and `thiz`: `nativeOpen` is `(String) -> Boolean`.
- `jclass` as the second parameter means "static": Kotlin `@JvmStatic external fun`. Instance and static are checked
  against Kotlin at startup, with a hint if they are the wrong way round.
- A `jobject` parameter does not say which Java class it is, so you say it: `native<&fn, void(ListenerJ)>`. The
  compiler checks that your Java types and the function agree.
- **Every class with members is added** with `r.add<C>()`, with natives or without. If you forget, `init` says so
  (§4); there is no silent half-registered class.
- `register_camera` calls no JNI: it only fills a list. `init` does the finding when all lists are ready.

Kotlin side, for completeness:

```kotlin
object NativeLoader { ... }            // jni-runtime provides it: NativeLoader.load("unified_native")

class Camera {
    private var nativeHandle: Long = 0
    private fun onFrame(index: Int, name: String) { ... }
    private external fun nativeOpen(url: String): Boolean
}
```

## 4. What startup tells you

`init` runs everything and collects the problems instead of stopping at the first. Real output, from the host tests:

```
method jnirt.test.Camera.twice (I)I: not found
  hint: a static method with this name and signature exists: use StaticMethod (Kotlin: @JvmStatic)

native method jnirt.test.Camera.staticOnly ()V: not found
  hint: the Kotlin method is static, but the C++ function takes jobject. Take jclass, or remove @JvmStatic.

class jnirt.test.Camera: `external fun forgotten` has no native function registered
  hint: add .native<&function>("forgotten") to r.add<...>() of this class.

class jnirt.test.Listener has members but is not added to the registry
  hint: add r.add<...>() for it in a register function.

class jnirt.test.Boom: not found (java.lang.ExceptionInInitializerError, caused by java.lang.IllegalStateException: no context)
  hint: the class exists, but its static initializer (static {} / companion object init) threw. FindClass runs it.

version mismatch: the Kotlin side of jni-runtime is 0.0.0-old, the native side is 0.1.0
```

The member line is `kind class.member descriptor`, the way `javap -s -p com.acme.camera.Camera` prints it, so you can
put them side by side. All problems come in one log, then `JNI_ERR`, then `System.loadLibrary` throws
`UnsatisfiedLinkError`, and `NativeLoader.load` points you to the log tag.

Typical causes, most common first:

| Message                                  | Usually                                                                 |
|------------------------------------------|-------------------------------------------------------------------------|
| `class ...: not found`                   | R8 renamed or removed it (`@NativeAccessed`, `consumer-rules.pro`), or a typo in `name` |
| `method ...: not found`, no hint         | renamed, or the types differ: `void(jint)` against `onFrame(Long)`       |
| `... not found` + static hint            | `@JvmStatic` missing (or present when it should not be)                  |
| `external fun ... has no native function` | a native was added in Kotlin and not in `register_xxx`                  |
| `has members but is not added`           | `r.add<C>()` forgotten                                                   |
| `version mismatch`                       | the Kotlin module and the native library come from different commits    |

## 5. How `init` knows your members (the one clever part)

C++ cannot ask a struct what members it has. So each `Field`, `Method` ... adds itself to the list of its class in its
own constructor. `static inline` members are built when the library is loaded, before `JNI_OnLoad`, so by the time
`init` runs every list is complete.

```
dlopen(libunified_native.so)
  CameraJ::nativeHandle  ->  "I am a member of CameraJ"      (list of CameraJ: [nativeHandle])
  CameraJ::onFrame       ->  "I am a member of CameraJ"      (list of CameraJ: [nativeHandle, onFrame])
JNI_OnLoad -> init       ->  for every added class: FindClass, then every member of its list
```

Kotlin analogy: a `companion object { init { registry += this } }`, where the "registry" is created on first use so it
always exists when the first member arrives.

Why this matters to you:

- You cannot forget a member: if it is written, it is looked up.
- `init` can also see which classes have members. That is how it notices a class you did not add.
- Do not call Java from static C++ objects or start threads that call Java before `init`: nothing is found yet. In debug
  builds the call stops with "was used before jnirt::init() found it" and names the member.

## 6. What the compiler refuses

The first line of the compiler output is the message; the rest is template noise. Recorded with gcc 13:

| You wrote                                               | You get                                                                                       |
|---------------------------------------------------------|-----------------------------------------------------------------------------------------------|
| `.native<&f>("x")`, `f` has a `jobject`                 | `native<&fn>: fn has a jobject, which does not say which Java class it is. Give the Java types: native<&fn, void(ListenerJ)>("name") ...` |
| `.native<&f, jint(jint)>("x")`, `f` takes two ints      | `native<&fn, J>: J and fn have a different number of parameters (fn's JNIEnv* and jobject / jclass are not counted)` |
| `.native<&f, jlong(jint, jint)>("x")`, `f` returns jint | `native<&fn, J>: the result of J does not match fn. A described class is a jobject in fn, every other type is the same on both sides` |
| `void f(JNIEnv*, jstring)`                              | `native<&fn>: the second parameter of fn must be exactly jobject (an instance method) or jclass (a static method)` |
| `void f(jobject)` (no `JNIEnv*`)                        | `native<&fn>: fn must be a plain function: R fn(JNIEnv *env, jobject thiz, params...) ...`    |
| `r.add<Plain>()`, `Plain` is not a `JavaClass`          | `add<C>(): C must be described as struct C : jnirt::JavaClass<C> { ... }`                     |
| `Method<CameraJ, jint>`                                 | `Method<C, F>: F must be a function type, like Method<CameraJ, void(jint, jstring)>`          |
| `Constructor<CameraJ, jint(jint)>`                      | `a constructor returns void in its signature: Constructor<CameraJ, void(jstring)>`            |

What the compiler cannot know is the Kotlin side; that is what startup is for (ADR-0005 §7, ADR-0006 §7).

## 7. What stays the same for you

- `jnirt::init(vm, {...})` is the whole `JNI_OnLoad`. One `.so`, one `JNI_OnLoad`, as before (docs/build-and-delivery.md).
- Classes, IDs and the global reference to each class live for the whole process; nobody deletes them, and nothing in
  static storage tries (ADR-0004).
- Calling Java from your own native thread works the same: `jnirt::env()` for the thread, then the same member calls.
  The IDs were found on the loading thread, once; the class reference is global.

## Where next

`entry()` wraps every registered native in the one `try/catch` that maps C++ errors to Java exceptions, and returns
`JavaException::throwable()` to Kotlin as it is; see [exceptions-and-logs-by-example.md](exceptions-and-logs-by-example.md). Strings and arrays you build by hand today (`NewStringUTF`)
become conversions in P5.
