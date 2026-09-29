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
│  └─ register.hpp            # void register_wificameras(jnirt::Registry&)
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
    return jnirt::init(vm, [](jnirt::Registry& r) {
        register_wificameras(r);
        register_nextlib(r);
    });
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

