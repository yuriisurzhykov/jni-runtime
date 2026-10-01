# Signatures by example

A learning guide to [ADR-0005](../adr/0005-signatures.md). How to say "this Kotlin method takes an `Int` and a
`String` and returns nothing" in C++, so that the compiler builds the JVM string for you and refuses what it can not
translate.

Some pieces come from later steps and are marked in comments: `Method` / `Field` and `jnirt::Object` (`class_cache`,
P3.5), `Registry::native` (P3.6). Only `descriptor` and `signature` exist today.

## The idea in one picture

```
Kotlin:  fun onFrame(index: Int, name: String)
C++:     void(jint, jstring)                  <- you write this
JVM:     (ILjava/lang/String;)V               <- jnirt::signature<void(jint, jstring)> builds this
```

You never write the third line. If the second line contains something the JVM has no name for, the code does not
compile.

Kotlin analogy: `typeOf<T>()` with `reified`. You pass a type, and the compiler turns it into something you can use
at run time. Here the "something" is the JVM signature string, and it is built entirely by the compiler.

## 1. The two things you use

```cpp
#include <jnirt/runtime/signature.h>

jnirt::descriptor<jlong>                  // "J"                        one type: a field
jnirt::signature<void(jint, jstring)>     // "(ILjava/lang/String;)V"   a method: parameters and result
```

Both are plain `const char*`, ready for JNI:

```cpp
env->GetFieldID(cls, "nativeHandle", jnirt::descriptor<jlong>);
env->GetMethodID(cls, "onFrame", jnirt::signature<void(jint, jstring)>);
```

In library code you will rarely write even this: `Field<CameraJ, jlong>` and `Method<CameraJ, void(jint, jstring)>`
(P3.5) take the type and call `descriptor` / `signature` themselves. The type you put there is what this guide is
about.

## 2. Which C++ type for which Kotlin type

| Kotlin parameter / result | Write in C++      | Comes out as            |
|---------------------------|-------------------|-------------------------|
| `Unit` (result)           | `void`            | `V`                     |
| `Boolean`                 | `jboolean`        | `Z`                     |
| `Byte`                    | `jbyte`           | `B`                     |
| `Char`                    | `jchar`           | `C`                     |
| `Short`                   | `jshort`          | `S`                     |
| `Int`                     | `jint`            | `I`                     |
| `Long`                    | `jlong`           | `J`                     |
| `Float`                   | `jfloat`          | `F`                     |
| `Double`                  | `jdouble`         | `D`                     |
| `String`                  | `jstring`         | `Ljava/lang/String;`    |
| `Class<*>`                | `jclass`          | `Ljava/lang/Class;`     |
| `Throwable`               | `jthrowable`      | `Ljava/lang/Throwable;` |
| `IntArray`, `ByteArray`, ... | `jintArray`, `jbyteArray`, ... | `[I`, `[B`, ... |
| any other class or interface | a description, see §3 | `Lcom/acme/...;`   |
| `Any`                     | `jnirt::Object` (P3.5) | `Ljava/lang/Object;` |

The rule: **`j` types, descriptions, and the types the conversion table knows** (`bool`, `std::string`, `std::optional<T>`, ... see
[type-conversion-by-example.md](type-conversion-by-example.md)). Not `int64_t` "because it is the same", not `jobject`.

## 3. Your own class

Kotlin:

```kotlin
package com.acme.camera

interface Listener { fun onFrame(frame: Frame) }
class Frame(val index: Int)
class Camera {
    enum class State { IDLE, STREAMING }
    fun setListener(listener: Listener) {}
    fun state(): State = State.IDLE
}
```

C++: one description per class that appears in a signature.

```cpp
struct ListenerJ : jnirt::JavaClass<ListenerJ> { static constexpr const char* name = "com/acme/camera/Listener"; };
struct FrameJ    : jnirt::JavaClass<FrameJ>    { static constexpr const char* name = "com/acme/camera/Frame"; };
struct StateJ    : jnirt::JavaClass<StateJ>    { static constexpr const char* name = "com/acme/camera/Camera$State"; };

jnirt::signature<void(ListenerJ)>   // "(Lcom/acme/camera/Listener;)V"
jnirt::signature<StateJ()>          // "()Lcom/acme/camera/Camera$State;"
```

Things to know:

- **The argument of `JavaClass` is the struct itself**: `struct FrameJ : jnirt::JavaClass<FrameJ>`. This is how the
  library recognises a description. Kotlin analogy: `class Frame : Comparable<Frame>`.
- **`name` is the JVM name**: slashes instead of dots, `$` before a nested class. The same name `javap` prints. A dot
  or a `;` in it does not compile.
- **Interfaces, enums and JDK classes are described the same way**: `"java/util/List"` is just another name.
- **A description used only in signatures needs only `name`.** Members (`Method`, `Field`) come with `class_cache`.
- **The suffix `J`** (`FrameJ`) is a convention: "the Java side of Frame", so it does not clash with your C++ `Frame`.

## 4. Why `jobject` does not compile

The tempting code:

```cpp
// Kotlin: fun setListener(listener: Listener)
jnirt::signature<void(jobject)>   // would be "(Ljava/lang/Object;)V": a different method
```

`jobject` means "some Java object" in C++, but in a signature it would have to mean exactly `java.lang.Object`. The
JVM looks methods up by exact signature, so `setListener(Object)` is not `setListener(Listener)`. The lookup would
fail at startup, and the reason is not obvious from the log.

So jnirt does not let you write it:

```
error: static assertion failed: jnirt: jobject does not say which Java class. Describe the class
(struct ListenerJ : jnirt::JavaClass<ListenerJ>) and use ListenerJ; for a real java.lang.Object use jnirt::Object.
```

The fix is in the message: describe `Listener` (§3) and write `void(ListenerJ)`. If the Kotlin parameter really is
`Any`, write `jnirt::Object`: a ready description of `java/lang/Object` that comes with `class_cache` (P3.5). You
can not type it by accident.

Kotlin analogy: an API that takes `listener: Any` compiles with anything, and the mistake shows up later. An API that
takes `listener: Listener` does not let the mistake in.

## 5. What the compiler stops, and what it can not

Real compiler output (gcc; clang says the same and also shows the type):

| You wrote                          | You get                                                                |
|------------------------------------|------------------------------------------------------------------------|
| `void(unsigned)`                   | `jnirt: Java has no unsigned numbers (size_t, uint32_t, uint64_t, unsigned). Take jint / jlong and check the range yourself.` |
| `void(jobject)`                    | `jnirt: jobject does not say which Java class. ...` (§4)               |
| `void(std::vector<int>)` before P5.2 | `jnirt: this C++ type has no Java type. Supported: void, bool, the JNI numbers ...` and how to add a row |
| `void(long long)`                  | `jnirt: this integer type is not one of the JNI types. Write jint, jlong, jshort, jbyte; ...` |
| `name = "com.acme.Camera"`         | `jnirt: JavaClass name must be in JVM form: "com/acme/Camera" ...`     |
| `struct B : JavaClass<A>` (wrong argument) | `jnirt: this C++ type has no JVM descriptor ...` with the type `B` named by the compiler |
| `signature<jint>` (not a function) | `jnirt: signature<F> needs a function type, like void(jint, jstring)` |

Why `long long` fails although it is 64 bits like `jlong`: on arm64 and x86_64 `jlong` is `long`, and for C++
`long` and `long long` are different types even when they have the same size. On armv7 `jlong` is `long long`, so
there it would compile, but every ABI is built, so you see the error anyway. Write `jlong`.

What the compiler **can not** stop:

- **`uint8_t` and `uint16_t`.** These are not "similar" to `jboolean` and `jchar`, they are the same types under
  another name. `void(uint16_t)` silently becomes `(C)V`. If you meant a number and Kotlin has `Short`, the lookup
  fails at startup with the exact method name (§6).
- **The wrong class, or a typo in `name`.** `ListenerJ` describing `"com/acme/camera/Listner"` compiles fine. The JVM
  is the only one who knows the real name.
- **A type that does not match Kotlin.** `void(jint)` against `fun onFrame(index: Long)`. The compiler sees C++ only.

## 6. Where the rest is caught: startup

Everything from the second list is caught by the lookups in `init` (P3.5, P3.6). They run once in `JNI_OnLoad`, and
a failed one stops the library from loading with one line that names the class, the member and the signature:

```
jnirt: method com.acme.camera.Camera.onFrame(ILjava/lang/String;)V not found.
  why: the method was renamed or removed, or R8 stripped it.
  fix: check the Kotlin name and signature; keep it with @NativeAccessed.
```

So the order of defence is: the compiler catches what has no JVM meaning, startup catches what does not match
Kotlin, and nothing is left for the first call in production. Code generation from Kotlin (P11) would move the second
group to compile time too.

Tip: `javap -s -p com.acme.camera.Camera` (on the compiled Kotlin class) prints the real JVM signatures. Compare them
with the line in the log.

## 7. Native functions (P3.6)

A native function is called by the JVM, so its parameters must be real JNI types:

```cpp
// Kotlin: private external fun nativeOpen(url: String): Boolean
jboolean nativeOpen(JNIEnv* env, jobject thiz, jstring url);
```

The registry takes the signature from the function type, after cutting off the first two parameters: they are always
there and are not part of the Java signature.

```
jboolean (JNIEnv*, jobject, jstring)
          ^^^^^^^^^^^^^^^^  cut off     ->  signature<jboolean(jstring)>  ->  "(Ljava/lang/String;)Z"
```

Kotlin analogy: an extension function `fun Camera.nativeOpen(url: String)`. It has a receiver, but the receiver is
not one of the parameters you see.

The rules, fixed in ADR-0005 §6:

1. The first parameter is exactly `JNIEnv*`.
2. The second is `jobject` for a normal method (it is the Kotlin `this`) or `jclass` for a static one (`@JvmStatic`,
   top-level). The registry checks at startup that this matches Kotlin.
3. The Java signature is built from the rest.
4. If the rest contains `jobject`, say which class it is:

```cpp
// Kotlin: private external fun nativeSubscribe(listener: Listener)
void nativeSubscribe(JNIEnv* env, jobject thiz, jobject listener);   // the JVM passes jobject, nothing else possible

r.add<CameraJ>()
 .native<&nativeOpen>("nativeOpen")                                 // nothing to say: all types are clear
 .native<&nativeSubscribe, void(ListenerJ)>("nativeSubscribe");     // jobject inside: give the Java type
```

The registry checks at compile time that `void(ListenerJ)` fits the C++ parameters (`ListenerJ` ↔ `jobject`,
everything else must be the same type). Leave out `void(ListenerJ)`, and you get the `jobject` error from §4.

## Common mistakes

| Mistake                                         | What happens                                   | Fix                                         |
|-------------------------------------------------|------------------------------------------------|---------------------------------------------|
| `unsigned` / `size_t` in a signature            | does not compile                               | `jint` / `jlong`, check the range yourself  |
| `jobject` for a Kotlin parameter of type `Listener` | does not compile                           | describe `ListenerJ`, write `void(ListenerJ)` |
| `name = "com.acme.Camera"`                      | does not compile                               | `"com/acme/Camera"`                         |
| `name = "com/acme/Camera.State"` for a nested class | lookup fails at startup                    | `"com/acme/Camera$State"`                   |
| `long long` / `int64_t` mixed with `jlong`      | `long long` does not compile on 64-bit         | `jlong`                                     |
| `uint16_t` for a Kotlin `Short`                 | `(C)` instead of `(S)`: lookup fails at startup | `jshort`                                   |
| `internal fun` called from native               | lookup fails: the JVM name is `name$module`    | make it `private` or `public`               |
| `companion object` function without `@JvmStatic` | lookup fails: it lives on `Camera$Companion` | add `@JvmStatic`                            |
| hand-written `"(I)V"` somewhere                 | works until someone changes Kotlin             | `jnirt::signature<void(jint)>`              |
