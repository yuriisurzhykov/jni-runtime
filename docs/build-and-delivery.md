# Native Architecture Overview

## Goal

Provide a reusable JNI foundation (`jni-runtime`) and let feature libraries (e.g. `wifi-cameras`) build on it.
Everything is compiled into **one shared library** owned by the `unified` app.

## Repositories

| Repo                            | Role                          | Contains                                            |
|---------------------------------|-------------------------------|-----------------------------------------------------|
| `unified`                       | Application and build owner   | Gradle app, single CMake entry, single `JNI_OnLoad` |
| `jni-runtime`                   | Domain-neutral JNI foundation | C++ runtime (static), Kotlin helpers (AAR/module)   |
| `wifi-cameras`, `next-lib`, ... | Feature libraries             | Pure C++ core, JNI bindings, Kotlin public API      |

Dependency direction (never upward):

```
unified → feature libraries → jni-runtime
```

## Layout of `unified`

```
unified/
├─ settings.gradle.kts        # includes app + submodule Gradle modules
├─ app/                       # Kotlin app, externalNativeBuild → native/
├─ native/
│  ├─ CMakeLists.txt          # the only native build entry
│  └─ entry/jni_onload.cpp    # the only JNI_OnLoad
├─ third_party/               # git submodules, pinned to commits/tags
│  ├─ jni-runtime/
│  ├─ wifi-cameras/
│  └─ next-lib/
└─ docs/adr/                  # unified-level decisions
```

## Layout of `jni-runtime`

```
jni-runtime/
├─ CMakeLists.txt             # targets: jnirt::support, jnirt::runtime
├─ include/jnirt/
│  ├─ support/                # no jni.h: log, error, assert
│  └─ runtime/                # jni.h allowed: vm, ref, cache, convert,
│                             # exception, peer, subscription, registry
├─ src/
├─ android/                   # Kotlin module: NativePeer, Subscription,
│                             # NativeLoader, consumer-rules.pro
├─ tests/                     # host (gtest) and android (minified build)
└─ docs/adr/0001-invariants.md
```

## Layout of a feature library

```
wifi-cameras/
├─ CMakeLists.txt             # STATIC target + alias wificameras::native
├─ native/
│  ├─ core/                   # pure C++, no jni.h, host-testable
│  ├─ bindings/               # JNI glue on top of jnirt::runtime
│  └─ register.h              # void register_wificameras(jnirt::Registry&)
├─ android/                   # Kotlin API + consumer-rules.pro, no native build
├─ standalone/                # optional: JNI_OnLoad for own tests/CI
└─ tests/
```

## Layers

- **core**: pure C++, uses only `jnirt::support` (logging, errors).
- **runtime**: `jnirt::runtime`, JNI mechanics (env, refs, caches, exceptions, peers).
- **bindings**: feature-specific JNI code on top of the runtime.

`jni.h` is allowed only in `runtime` and `bindings`. CI checks that `core/` never includes it.

## Build wiring

```cmake
# unified/native/CMakeLists.txt
add_subdirectory(../third_party/jni-runtime jnirt)
add_subdirectory(../third_party/wifi-cameras wificameras)
add_subdirectory(../third_party/next-lib nextlib)

add_library(unified_native SHARED entry/jni_onload.cpp)
target_link_libraries(unified_native PRIVATE
        wificameras::native nextlib::native jnirt::runtime)
```

```cpp
// unified/native/entry/jni_onload.cpp
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    return jnirt::init(vm, {register_wificameras, register_nextlib});
}
```

Gradle:

```kotlin
include(":app")
include(":jnirt-kotlin"); project(":jnirt-kotlin").projectDir = file("third_party/jni-runtime/android")
include(":wifi-cameras"); project(":wifi-cameras").projectDir = file("third_party/wifi-cameras/android")
```

Library Gradle modules contain Kotlin only, with no `externalNativeBuild`.

## Rules

1. **One `.so`, one `JNI_OnLoad`**, owned by `unified`.
2. **Feature libraries are static components**, not shared libraries.
3. **No nested submodules.** Only `unified` pins the `jni-runtime` version.
4. **Libraries never fetch the runtime themselves.** They link `jnirt::runtime` and expect it to exist. Standalone mode
   uses an `if(NOT TARGET jnirt::runtime)` guard.
5. **Libraries talk to each other only through Kotlin APIs.** No native objects, handles, or refs cross library
   boundaries.
6. **Common flags** (`-fvisibility=hidden`, warnings) come from `jnirt::runtime` via `INTERFACE` options.
7. **`System.loadLibrary` is called in one place**: `NativeLoader.load("unified_native")` in `app`.
8. **ProGuard**: each library ships its own `consumer-rules.pro`; native-accessed classes are marked with
   `@NativeAccessed`.

## Working on a library in the IDE

Android Studio indexes C++ only for Gradle modules with `externalNativeBuild`. By rule 1 only `unified` has one, so a
library repository opened on its own shows its C++ as plain text. The fix is a development-only `.so` in the library
module, switched on by a Gradle property:

```kotlin
// wifi-cameras/android/build.gradle.kts
android {
    // Library development only: C++ indexing and own device tests. Off inside unified.
    if (providers.gradleProperty("jnirt.standaloneNative").orNull == "true") {
        externalNativeBuild { cmake { path = file("../standalone/CMakeLists.txt") } }
        defaultConfig { ndk { abiFilters += "arm64-v8a" } }
    }
}
```

```properties
# wifi-cameras/gradle.properties (root of the library repository)
jnirt.standaloneNative=true
```

```cmake
# wifi-cameras/standalone/CMakeLists.txt
cmake_minimum_required(VERSION 3.22)
project(wificameras_standalone LANGUAGES CXX)
add_subdirectory(../third_party/jni-runtime jnirt)   # or wherever the runtime is checked out for development
add_subdirectory(.. wificameras)
add_library(wificameras_dev SHARED jni_onload.cpp)   # a name different from unified_native
target_link_libraries(wificameras_dev PRIVATE wificameras::native jnirt::runtime)
```

Gradle reads `gradle.properties` only from the root of the build. When `unified` includes the library module, the
root is `unified`, the property is not set, and the app still gets one `.so` with one `JNI_OnLoad`. `jni-runtime/android`
uses the same setup with a small `standalone/` whose `JNI_OnLoad` calls `jnirt::init(vm, {})`.

A CI step in `unified` checks that the APK has exactly one of our `.so` exporting `JNI_OnLoad`:

```
Found 2 native libraries with JNI_OnLoad: libunified_native.so, libwificameras_dev.so.
why: jnirt.standaloneNative=true is set in unified/gradle.properties; it is meant only for a library opened on its own.
fix: remove jnirt.standaloneNative from unified/gradle.properties.
```

Do not split libraries into separate `.so` files in the app to fix indexing (see ADR-0003 §7).

### Red `external fun`

The inspection "Cannot resolve corresponding JNI function" looks for a C function named
`Java_<package>_<class>_<method>`. Natives are registered with `RegisterNatives` (ADR-0001 §7.4), so such names
never exist, whatever the build setup. Suppress it on the class with native methods:

```kotlin
@Suppress("KotlinJniMissingFunction") // natives are registered via RegisterNatives
internal class CameraNative { external fun connect(host: String): Long }
```

Keep the native function named like the Kotlin method, and its registration in `bindings/<Class>_jni.cpp`, so a
name search finds it. That every `external fun` is registered is checked by the self-check in `JNI_OnLoad`, not by
the IDE. Real navigation comes with codegen (P11).

## Submodule workflow

- Each submodule is pinned to a specific commit or tag.
- Change flow: commit in the library repo, push, then bump the submodule in `unified`.
- CI runs `git submodule update --init` (no `--remote`), builds all modules, and tests on a minified (R8) build.

## Adding a new library

1. Create the repo using the feature-library layout above.
2. Add it under `third_party/` as a submodule.
3. Add `add_subdirectory(...)` and link its `::native` target in `unified/native/CMakeLists.txt`.
4. Add its `register_xxx()` call in `jni_onload.cpp`.
5. Include its Kotlin module in `settings.gradle.kts`.

