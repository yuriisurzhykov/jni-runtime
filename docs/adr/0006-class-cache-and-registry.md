# ADR-0006 - Class cache, registry and startup

## Status

Proposed. Implementation: roadmap P3.4, P3.5, P3.6. Explained with examples:
[learning/class-cache-and-registry-by-example.md](../learning/class-cache-and-registry-by-example.md).

## Context

ADR-0001 §4.1 says every `jclass`, `jmethodID` and `jfieldID` is resolved once in `JNI_OnLoad`, §7.4 says natives are
registered with `RegisterNatives`, §9.1 says startup fails fast when the Kotlin side does not match. Done by hand this
is the most repetitive and most error-prone part of JNI: `FindClass`, `NewGlobalRef`, `DeleteLocalRef`, a `GetMethodID`
with a hand-written signature, a null check, a `JNINativeMethod[]`, and an error message that says nothing about *which*
name was wrong. A typo is found in production, on the first call, on a thread that may not even see the class.

Three decisions were already made and are only applied here: signatures come from C++ types (ADR-0005), references are
RAII (ADR-0004), a Java exception thrown by a call from native code is cleared and turned into a C++ exception that
holds the original `Throwable`.

Class cache and registry are one design because they share one startup, one pass over the classes and one report of
what went wrong. The dependency is one-way: registry uses the cache, the cache knows nothing about registry.

## Decision

### 1. What is described, and how

A Java class is described once, next to the bindings, by a struct. Its members are `static inline` objects:

```cpp
struct CameraJ : jnirt::JavaClass<CameraJ> {
    static constexpr const char* name = "com/acme/camera/Camera";           // JVM form, '$' for nested classes

    static inline jnirt::Field<CameraJ, jlong>                 nativeHandle{"nativeHandle"};
    static inline jnirt::StaticField<CameraJ, jint>            instances{"instances"};
    static inline jnirt::Method<CameraJ, void(jint, jstring)>  onFrame{"onFrame"};
    static inline jnirt::StaticMethod<CameraJ, jint(jint)>     maxFrames{"maxFrames"};
    static inline jnirt::Constructor<CameraJ, void(jstring)>   create;
};
```

| Member                       | Kotlin                                                       | Use                                         |
|------------------------------|--------------------------------------------------------------|---------------------------------------------|
| `Field<C, T>`                | a property with a backing field (`@JvmField`, `private var`) | `.get(env, obj)`, `.set(env, obj, v)`       |
| `StaticField<C, T>`          | `@JvmField` / `const val` in a companion, top-level `val`    | `.get(env)`, `.set(env, v)`                 |
| `Method<C, R(A...)>`         | an instance function                                         | `m(env, obj, args...)`                      |
| `StaticMethod<C, R(A...)>`   | `@JvmStatic` function                                        | `m(env, args...)`                           |
| `Constructor<C, void(A...)>` | a constructor (no name to write)                             | `c(env, args...)` gives `LocalRef<jobject>` |

Types are those of ADR-0005. A parameter, result or field that is one of your classes is written as its description
(`ListenerJ`) and travels as a `jobject`; `jnirt::Object` is `java.lang.Object`. `JavaClass` is defined here
(`class_cache.h`); `signature.h` only declares it.

`JavaClass<C>::cls()` is the class itself, a global reference valid for the process lifetime.

### 2. Members register themselves

C++ has no reflection, so `init` can not ask `CameraJ` which members it has. Each member's constructor adds the member
to the list of its class. `static inline` members are constructed when the library is loaded, before `JNI_OnLoad`, so
the list is complete when `init` runs.

- The list lives in a `ClassInfo` that is created by a function-local static (`classInfo<C>()`), so the order of static
  initialization between files does not matter: a member can never be added to a list that does not exist yet.
- `ClassInfo` is allocated with `new` and never deleted. It holds the global reference to the class (ADR-0004: nothing
  in static storage destroys a reference at exit, when the VM may be gone).
- Checked on gcc 13 and clang, `-O0` and `-O2`, through a static library: when the linker keeps any file that includes
  the description, **all** members of that class are registered. When it keeps none, nobody calls the members either.
  The dangerous case "called but not registered" does not exist.
- A member that is written is looked up. A dead description is a startup error, which is intended.

### 3. Calls

- `env` is always the first argument (as with `LocalRef`); no hidden `jnirt::env()` on the hot path.
- Arguments are JNI types (`jstring`, `jint`, `ref.get()`); results are `void`, a primitive, or a `LocalRef<T>` for a
  reference. Nobody writes `DeleteLocalRef`.
- The JNI call is chosen at compile time from the type (`CallIntMethod`, `GetStaticLongField`, ...), through the
  variadic JNI calls: the spec fixes the argument promotions and the compiler applies the same ones.
- **A Java exception after a call** is cleared and thrown as `jnirt::JavaException` (§4). Pending exceptions never
  survive a call, so ADR-0001 §6.5 can not be broken by forgetting a check.
- A null object is `JNIRT_REQUIRE` (`IllegalArgument`) in debug and release: the cost is one compare, the alternative is
  a crash inside the JVM.
- **Debug only:** `IsInstanceOf(obj, class)` before an instance call or field access, and a check that the member was
  found. A call before `init` fails with "was used before jnirt::init () found it" instead of passing a null ID to JNI.
- A class may name itself in its own members: `Method<CameraJ, CameraJ()>`. Whether a type is a "described class" is
  decided without looking inside it (`JniForm<T>`: arithmetic and pointer types are themselves, anything else is
  `jobject`), because `CameraJ` is incomplete while its members are declared; `descriptor<CameraJ>` is only built when
  `init` needs it.

### 4. `JavaException`

```cpp
class JavaException : public std::exception {
    jthrowable  throwable() const noexcept;   // the original, for entry() (ADR-0007) to hand back to Kotlin unchanged
    const char* what() const noexcept;        // Throwable.toString(), taken when it was caught
};
```

It owns the `Throwable` as a global reference inside a `shared_ptr`, so it is usable on any thread and copying never
throws (the same rule as `jnirt::Error`). `toString()` is called once, at the catch, so logging it later needs no
JNI. If `toString()` itself throws, the text is `<a Java exception whose toString() threw>`: no recursion.

This is the half of `exception.h` that class_cache needs. C++ error to Java exception, and `entry()`, are ADR-0007.

### 5. Registry

```cpp
void register_camera(jnirt::Registry& r) {
    r.add<CameraJ>()
     .native<&nativeOpen>("nativeOpen")                                   // jboolean(jstring) from the function
     .native<&nativeSubscribe, void(ListenerJ)>("nativeSubscribe");       // a jobject needs its Java type
    r.add<ListenerJ>();                                                   // no natives; its methods are called
}
```

- `Registry` only collects data. It calls no JNI while the register functions run.
- **Every class that has members, and every class with natives, is added with `r.add<C>()`.** `init` compares the two
  lists (§6, step 4) and reports a described class that was not added. It can not be done at compile time: the
  compiler sees one file at a time and does not know the descriptions in the others. The explicit `add` also makes the
  list of classes of a library readable in one place.
- A class that only stands in a signature (`ListenerJ` as a parameter, never used for a call) has no members and needs
  nothing.
- `add<C>()` twice for the same `C` continues the same class (two modules may use one description). The same name and
  signature registered twice is an error.
- `native<&fn>` implements ADR-0005 §6: the function must be `R fn(JNIEnv*, jobject|jclass, params...)` (`noexcept` is
  accepted), the Java signature is `R(params...)`, `jclass` marks a static method, a `jobject` needs the explicit Java
  type and it is checked against the function at compile time (same arity, same JNI form of each type). Each of these
  fails with a message that says what to write; the first line of the compiler output is that message.
- `native<>` registers an `entry()` wrapper instead of `fn` (ADR-0007 §1); the call site did not change.

### 6. `init` and the startup

```cpp
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) { return jnirt::init(vm, {register_camera, register_other}); }
```

The list of functions is an `initializer_list`; `init(vm, {})` is valid. Steps, in this order:

1. `setVm(vm)`; run the register functions (data only).
2. jnirt's own classes: `java.lang.Throwable` (to describe exceptions) and `jnirt.NativeLoader`.
3. The version check (§7). If 2 or 3 fail, **stop**: everything after is noise.
4. Every class with members that is not added: a problem.
5. For every added class: `FindClass`, a global reference, then every member. A class that is not found is one problem
   and its members are not searched.
6. For every native: check that the method exists with the kind (static or instance) that the C++ function
   implies, then `RegisterNatives` one native at a time, so a failure names the method.
7. The `external fun` check (§7).
8. No problems: `JNI_VERSION_1_6`. Otherwise log everything, `JNI_ERR`.

Problems are **collected, never thrown**: one run shows everything that is wrong. A startup that fails leaves the
library unusable (`System.loadLibrary` throws `UnsatisfiedLinkError`); nothing is rolled back.

A second call to `init` is `JNIRT_FATAL`. `JNI_OnLoad` runs once per library, and a different list the second time
means a mistake in the code. This closes the P0 item "`JNI_OnLoad` failure policy".

#### The report

```
E jnirt: startup failed, 3 problem(s), nothing native will work:
E jnirt: 1. class com.acme.camera.Preview: not found (java.lang.NoClassDefFoundError: com/acme/camera/Preview)
  hint: the name is wrong, the class was renamed or removed, or R8 stripped it.
E jnirt: 2. method com.acme.camera.Camera.twice (I)I: not found
  hint: a static method with this name and signature exists: use StaticMethod (Kotlin: @JvmStatic)
E jnirt: 3. class com.acme.camera.Camera: `external fun forgotten` has no native function registered
  hint: add .native<&function>("forgotten") to r.add<...>() of this class.
E jnirt: why: a class or member was renamed, removed or stripped by R8, or the C++ type does not match Kotlin.
  fix: compare with `javap -s -p <class>`; keep what native code uses (consumer-rules.pro, @NativeAccessed).
```

- A problem is `kind class.member descriptor`, the form `javap -s` prints, so it can be compared character by
  character. Classes are written with dots and `$`, as in a stack trace.
- A `hint:` appears only where the cause is known: the other kind exists (static/instance), the static initializer
  threw (`FindClass` runs it; the text shows the `ExceptionInInitializerError` and its cause), the method is not
  `external`. A hint for "a method with this name but another signature exists" is not given: it needs reflection over
  all methods. `javap` in the `fix:` line covers it.
- `why` and `fix` are printed once, at the end.

### 7. Checks of the Kotlin side

There is no separate "selfcheck" pass: steps 5 and 6 are the check that everything described in C++ exists in Kotlin.
Two more things are checked because lookups can not see them:

- **Version.** `jnirt.NativeLoader.VERSION` (a `const val`, a public static field) must equal the version the native
  runtime was built with (`JNIRT_VERSION`, the CMake project version). Done in native code so that nobody can bypass
  it by calling `System.loadLibrary` directly. A missing field means R8 removed it: `consumer-rules.pro` did not
  apply, and that shows at startup too.
- **`external fun` that nobody registered.** Without it, a forgotten `native<>` is an `UnsatisfiedLinkError` on the
  first
  call, in production (there are no `Java_...` names to link, ADR-0001 §7.4). `init` asks
  `NativeLoader.nativeMethodNames(Class)` for the names of the native methods of each added class and compares **names**
  with what was registered (overloads of an `external fun` in one class are rare, and comparing signatures
  would need reflection types). The reflection is in Kotlin (a few lines), where it is natural; native code only sees
  a `String`, which the table of ADR-0005 already supports. A class that has `external fun` but was never added is not
  seen; only code generation (P11) catches that.

Both always run, debug and release; they touch a handful of classes. Measure `init` on Edge II with the first library,
and make the `external fun` check debug-only if it matters.

### 8. Threads and order

| Phase                  | Thread                          | What is written                  |
|------------------------|---------------------------------|----------------------------------|
| library load           | one (the loader holds its lock) | the lists of members and classes |
| `init` in `JNI_OnLoad` | one                             | class references, IDs, natives   |
| everything after       | any                             | nothing: read-only               |

No locks and no atomics (except the once-only flag of `init`): a native method can not run before `RegisterNatives`,
and a thread created after `init` sees everything written before it. The rule for developers: **do not use Java from
the constructors of static C++ objects, and do not start threads that call Java before `init`.** The debug check
"used before init" names the member if it is broken.

### 9. Kotlin

`android/.../NativeLoader.kt`: `const val VERSION`, `load(name)` (the one place that calls `System.loadLibrary`,
with a pointer to the `jnirt` log tag when it fails), `nativeMethodNames(Class)`. `consumer-rules.pro` keeps the
class: R8 sees no Kotlin caller for the version and the method.

### 10. Not in this step

| What                                           | Why / when                                                                               |
|------------------------------------------------|------------------------------------------------------------------------------------------|
| `CallNonvirtual*Method` (`super.foo()`)        | the one JNI call kind not covered; native code almost never needs it; on demand          |
| `jobjectArray` in members                      | P5, with the element type (ADR-0005 §9)                                                  |
| typed references (`LocalRef<CameraJ>`)         | compile-time object checks; the debug `IsInstanceOf` covers it for now                   |
| lookup of a class by a different `ClassLoader` | `FindClass` in `JNI_OnLoad` sees the app loader; a library that needs another asks in P9 |
| `entry()` around natives                       | done: ADR-0007                                                                           |

## Consequences

- A library writes descriptions and `register_xxx`; no `FindClass`, `GetMethodID`, `JNINativeMethod`, signature string
  or `DeleteLocalRef` appears in it.
- Every Kotlin/native mismatch that can be seen without running the feature is a `JNI_ERR` at startup with the exact
  name, found in one run.
- `jnirt_runtime` gets `class_cache.cpp`, `exception.cpp`, `registry.cpp`, `selfcheck.cpp`, `init.cpp`.
- Host tests need Java classes: `tests/host/java` is compiled with `javac` by CMake, with a stand-in for
  `jnirt.NativeLoader` that has the version of the build. The Kotlin file itself is not compiled on the host; keep
  its two members in step with the stand-in.
- The Kotlin `VERSION` is edited by hand together with the CMake version until a release process (P10) automates it.

### Tests (host, real JVM, `-Xcheck:jni`)

| Check                                                                                                                                                     | Where                                                                            |
|-----------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------|
| every primitive: instance/static field and method round trip                                                                                              | `class_cache_test`                                                               |
| object results and fields, constructors, interface call, self reference                                                                                   | `class_cache_test`                                                               |
| `JavaException`: text, cleared exception, copies, global reference count                                                                                  | `class_cache_test`                                                               |
| null object, wrong object (debug)                                                                                                                         | `class_cache_test`                                                               |
| every startup problem has an exact text; all problems at once                                                                                             | `class_cache_test`, `registry_test`                                              |
| natives registered and called through Java; static/instance mismatch; not native; duplicates; forgotten `external fun`; class not added; version mismatch | `registry_test`                                                                  |
| `init` success, second call is a crash                                                                                                                    | `init_test`                                                                      |
| signature rules of `native<>`                                                                                                                             | `static_assert` in `registry_test`; failing cases recorded in the learning guide |
