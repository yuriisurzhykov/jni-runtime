# ADR-0005 - Signatures from C++ types

## Status

Proposed. Implementation: roadmap P3.3. Explained with examples:
[learning/signatures-by-example.md](../learning/signatures-by-example.md).

## Context

Every `GetMethodID`, `GetFieldID` and `RegisterNatives` call takes a JVM signature string such as
`"(ILjava/lang/String;)V"`. Written by hand, these strings are the most common JNI typo: nothing checks them until the
lookup returns `nullptr` at run time, and the error says nothing about which character is wrong.

ADR-0001 §7.2 requires signatures to be **generated at compile time from C++ types, never written by hand**. The
`class_cache` (P3.5) and `registry` (P3.6) are built on this: `Method<CameraJ, void(jint, jstring)>` and
`native<&nativeOpen>` must produce their strings without a JVM.

Two failure modes drive the design:

- **A type that has no JVM meaning** (`bool`, `std::string`, `long long`) must not compile, with a message that says
  what to write instead.
- **`jobject` says nothing about the Java class.** A Kotlin `fun setListener(l: Listener)` has the signature
  `(Lcom/acme/Listener;)V`; a C++ `void(jobject)` gives `(Ljava/lang/Object;)V`, and the lookup fails. If a wrong way
  exists, it will be used, so the wrong way has to be closed, not documented.

## Decision

### 1. API

```cpp
// <jnirt/runtime/signature.h>
namespace jnirt {
    template <class Self> struct JavaClass;                 // declared here, defined in class_cache.h (P3.5)

    template <class T> constexpr const char* descriptor;    // descriptor<jlong>                == "J"
    template <class F> constexpr const char* signature;     // signature<void(jint, jstring)>   == "(ILjava/lang/String;)V"
}
```

That is the whole public surface. Both values are ordinary null-terminated `const char*` that point to strings the
compiler built and placed in the binary; using them costs nothing at run time. Everything else lives in
`jnirt::detail`.

### 2. The type table

A type maps to its descriptor through one table: a class template `detail::Descriptor<T>` with one specialization per
supported type. The table is **closed**: code outside `signature.h` does not add rows to it.

| C++ type                                    | Descriptor               | Kotlin                    |
|---------------------------------------------|--------------------------|---------------------------|
| `void` (result only)                        | `V`                      | `Unit`                    |
| `jboolean`                                  | `Z`                      | `Boolean`                 |
| `jbyte`                                     | `B`                      | `Byte`                    |
| `jchar`                                     | `C`                      | `Char`                    |
| `jshort`                                    | `S`                      | `Short`                   |
| `jint`                                      | `I`                      | `Int`                     |
| `jlong`                                     | `J`                      | `Long`                    |
| `jfloat`                                    | `F`                      | `Float`                   |
| `jdouble`                                   | `D`                      | `Double`                  |
| `jstring`                                   | `Ljava/lang/String;`     | `String`                  |
| `jclass`                                    | `Ljava/lang/Class;`      | `Class<*>`                |
| `jthrowable`                                | `Ljava/lang/Throwable;`  | `Throwable`               |
| `jbooleanArray` ... `jdoubleArray`          | `[Z` ... `[D`            | `BooleanArray` ... `DoubleArray` |
| `C`, where `C : JavaClass<C>`               | `L<C::name>;`            | any class or interface    |

Signatures: `signature<R(A1, ..., An)>` is `"(" + descriptor<A1> + ... + descriptor<An> + ")" + descriptor<R>`.

### 3. Your own class: `JavaClass<Self>`

The only way to put a class that is not in the table into a signature is to describe it:

```cpp
struct ListenerJ : jnirt::JavaClass<ListenerJ> {
    static constexpr const char* name = "com/acme/camera/Listener";
};
// signature<void(ListenerJ, jint)>  ==  "(Lcom/acme/camera/Listener;I)V"
```

- `C` must derive from `JavaClass<C>`, with itself as the argument. `name` is a `static constexpr const char*` in JVM
  form: slashes, `$` for nested classes (`"com/acme/Camera$State"`).
- `name` is checked at compile time: not empty, no `.`, no `;`, no `[`, so `"com.acme.Camera"` and
  `"Lcom/acme/Camera;"` do not compile.
- A description used only as a parameter type needs nothing but `name`. It works for Kotlin classes, interfaces and
  JDK / SDK classes (`"java/util/List"`) alike.
- A class can name itself in its own members (`Method<CameraJ, CameraJ()>`): a signature is built only when it is
  used, when `CameraJ` is already complete.
- `JavaClass` is only **declared** in `signature.h`; `class_cache.h` (P3.5) defines it and includes `signature.h`.
  The dependency points one way: cache → signature.

Why the table is closed: a public extension point (`template <> struct Descriptor<MyType>`) would be a second way to
add a type, and one that lets any string be written by hand, which is what ADR-0001 §7.2 forbids.

### 4. `jobject` is not allowed in signatures

`descriptor<jobject>` does not compile:

```
jnirt: jobject does not say which Java class. Describe the class (struct ListenerJ : jnirt::JavaClass<ListenerJ>)
and use ListenerJ; for a real java.lang.Object use jnirt::Object.
```

- A parameter or result of Kotlin type `Any` is written `jnirt::Object`, a ready description of `java/lang/Object`
  that `class_cache.h` provides (P3.5). It can only be written on purpose.
- `jweak` is the same C++ type as `jobject` and is rejected the same way.
- `jstring`, `jclass`, primitive arrays are unambiguous and stay. `jthrowable` means exactly `Throwable`; a method that
  takes `IOException` needs a description, and a mistake there is caught by the lookup at startup (§7).

### 5. Only exact JNI types

The table matches the exact C++ type. JNI types are `typedef`s, so:

| Written       | Result                                                                                     |
|---------------|--------------------------------------------------------------------------------------------|
| `bool`        | does not compile: `jnirt: use jboolean, not bool`                                           |
| `char`, `char16_t`, `std::string`, any non-JNI type | does not compile: `jnirt: this C++ type has no JVM descriptor ...`   |
| `long long`   | does not compile on 64-bit ABIs (there `jlong` is `long`); on armv7 it is `jlong`. Every ABI is built, so the error always surfaces |
| `int`, `jsize`, `int64_t` | the same types as `jint` / `jlong`: correct                                      |
| `uint8_t`, `uint16_t` | the same types as `jboolean` / `jchar`: **not detectable**. A wrong one fails the lookup at startup (§7) |

Write only `j` types in signatures. Conversions (`bool` ↔ `jboolean`, `std::string` ↔ `jstring`) are P5.

### 6. Native functions (rules for `registry`, P3.6)

A native function that `Registry` registers must have the shape

```
R fn(JNIEnv* env, X self, P1, ..., Pn)
```

1. The first parameter is exactly `JNIEnv*`. Checked at compile time.
2. The second is exactly `jobject` (instance method, the Kotlin `this`) or `jclass` (static: `@JvmStatic` or
   top-level). Checked at compile time.
3. The Java signature is `signature<R(P1, ..., Pn)>`. The first two parameters are never part of it.
4. If `R` or any `Pi` is `jobject`, the Java type must be given explicitly: `native<&fn, R'(P1', ..., Pn')>`. The
   registry checks at compile time that every C++ type is the JNI form of the Java one (`ListenerJ` ↔ `jobject`,
   primitives and `jstring` / `jclass` / arrays ↔ themselves) and that the counts match. Without the explicit type
   the `jobject` rule of §4 fires.
5. At startup `init` checks rule 2 against Kotlin: a `jobject` function must be found by `GetMethodID`, a `jclass`
   one by `GetStaticMethodID`. A mismatch is an error that suggests `@JvmStatic`.

Rules 1-4 are a template in `registry.h`, rule 5 is `init`; `signature.h` knows nothing about native functions. The
same "your class in a signature is `jobject` in C++" rule is used by the typed calls of `class_cache` (P3.5).

### 7. What remains a startup error

The compiler can not know the Kotlin side. These are caught by the lookups in `init` (P3.5, P3.6), which fail with
`JNI_ERR` and the exact class, member name and signature in the log:

- a description that is valid but names the wrong class, or a typo in `name`;
- `uint8_t` / `uint16_t` meant as numbers;
- a type that does not match Kotlin (`void(jint)` against `onFrame(Long)`);
- a static / instance mismatch of a native function.

Only code generation from Kotlin (P11) removes these entirely. Until then selfcheck in host tests catches them on
every CI run.

### 8. Implementation notes

- C++17, as the rest of the project. The string type is `detail::Text<N>`: a `char[N + 1]` whose length is part of the
  type, with a `constexpr` `+`. A signature is a fold over the parameter descriptors.
- An unsupported type fails through `static_assert` in the primary template of `Descriptor`, guarded by a
  `T`-dependent condition. A `static_assert` in a full specialization (`template <> struct Descriptor<bool>`) would
  fire on every build, used or not; the special messages for `bool` and `jobject` therefore live in the primary
  template.
- `signature<F>` requires a function type; anything else fails with
  `jnirt: signature<F> needs a function type, like void(jint, jstring)`.

### 9. Not in P3.3

| What                                       | When / why                                                                 |
|--------------------------------------------|----------------------------------------------------------------------------|
| `jobjectArray`                             | P5: needs the element type (`Array<FrameJ>` → `[Lcom/acme/camera/Frame;`)   |
| `std::string`, `bool`, collections, `ByteBuffer` | P5 conversions                                                       |
| `jnirt::Object`, `JavaClass` definition    | done in P3.5 ([ADR-0006](0006-class-cache-and-registry.md))                 |
| native-function rules (§6)                 | done in P3.6 (ADR-0006 §5)                                                  |
| human-readable signatures in error messages (`onFrame(int, String)`) | not needed: the JVM form is what the developer greps for in `javap -s` |

## Consequences

- No signature string is written by hand anywhere in jnirt or in libraries built on it.
- `signature.h` is header-only; `jnirt_runtime` gets no new source file.
- `class_cache` and `registry` take `descriptor<T>` / `signature<F>` and never build strings themselves.

### Tests (host)

| Check                                              | How                                                                  |
|----------------------------------------------------|----------------------------------------------------------------------|
| every row of the table                             | `static_assert` on the descriptor: the file compiling is the test    |
| signatures: no parameters, several, arrays, own classes, a class naming itself | `static_assert`                        |
| the table agrees with a real JVM                    | `GetMethodID` / `GetStaticMethodID` / `GetFieldID` on JDK classes with generated signatures (real JVM, `-Xcheck:jni`) |
| rejected types (`bool`, `jobject`, `std::string`, bad `name`) | checked by hand once; the compiler output is recorded in the learning document |
