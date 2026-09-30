# Logging

`jnirt` provides a small, platform-independent C++ logging layer. On Android it
writes to logcat via `liblog`. It needs no `JNIEnv` or `JavaVM` and can be called
from any thread, including threads never attached to the JVM.

## Quick start

```cpp
// wifi_camera.cpp
#include <jnirt/support/logging/macros.h>

LOG_DEFINE_TAG("WifiCamera")

void connect(const char* host, int port) {
  LOGI("connecting to %s:%d", host, port);
  LOGE("connect failed: rc=%d", rc);
}
```

`LOG_DEFINE_TAG` is written once per translation unit, at namespace scope,
before the first `LOGx` call. There is no variable to name or remember.

## Levels

| Macro  | Level   | Typical use                                 |
|--------|---------|---------------------------------------------|
| `LOGV` | Verbose | Very chatty tracing (per-frame, per-packet) |
| `LOGD` | Debug   | Development diagnostics                     |
| `LOGI` | Info    | Lifecycle events, state changes             |
| `LOGW` | Warn    | Recoverable problems                        |
| `LOGE` | Error   | Failures                                    |

The level check happens **before** the arguments are evaluated, so
`LOGD("%s", expensive())` does not call `expensive()` when Debug is disabled.

## Choosing a tag

### Per file

Put `LOG_DEFINE_TAG("...")` at the top of the `.cpp`, as in the quick start.

### Per module (one tag for many files)

Create a private header inside the module and include it from every `.cpp`:

```cpp
// src/wifi_camera/module_log.h     (private, NOT in include/)
#pragma once
#include "jnirt/support/logging/macros.h"

LOG_DEFINE_TAG("WifiCamera")
```

```cpp
// src/wifi_camera/onvif_client.cpp
#include "wifi_camera/module_log.h"

void discover() { LOGI("probing network"); }
```

To give one file its own tag, do not include the module header there. Use
`LOG_DEFINE_TAG` directly instead. Defining a tag twice in one translation unit
is a compile error.

### Rules

- Use `LOG_DEFINE_TAG` at namespace scope only (not inside a function or class).
- Do not put `LOG_DEFINE_TAG` or `LOGx` calls in **public** headers. The tag
  would leak into consumers' translation units.
- Tags are string literals. Keep them short and stable: they are what you filter by.

## Formatting

Formatting is `printf`-style, and the compiler checks it (`-Wformat`).

| Type               | Specifier                               |
|--------------------|-----------------------------------------|
| `int`              | `%d`                                    |
| `size_t`           | `%zu`                                   |
| `int64_t`          | `%" PRId64 "` (from `<cinttypes>`)      |
| `double`           | `%f`, `%.2f`                            |
| `const char*`      | `%s`                                    |
| `std::string`      | `%s` with `.c_str()`                    |
| `std::string_view` | `%.*s` with `(int)sv.size(), sv.data()` |
| pointer            | `%p`                                    |

Never pass external data as the format string:

```cpp
LOGI(userText);        // wrong: '%' in the data becomes a format directive
LOGI("%s", userText);  // right
```

## Limits and behavior

- Messages longer than **1024 bytes** are truncated. There is no heap
  allocation on the logging path.
- A message is formatted into a fixed stack buffer, so it is safe to log from
  constrained contexts, but keep it in mind for threads with small stacks.
- Logging is thread-safe. Calls are `noexcept`.
- Logs from C++ go to logcat only. They do not pass through Timber or its
  `FileTree` on the Kotlin side.

## Adding the dependency

`jnirt` is a static library with a public include root (`include/`) and a
private one (`src/`). Consumers only see `include/`.

If `jni-runtime` is a git submodule of the consuming repo:

```cmake
# consumer CMakeLists.txt
add_subdirectory(external/jni-runtime)   # path to the submodule

add_library(wifi_camera STATIC src/onvif_client.cpp)
target_link_libraries(wifi_camera PRIVATE jnirt)
```

Linking `jnirt` provides the include path (`#include "jnirt/..."`) and the C++17
requirement automatically. For the final shared library assembled by the app,
link the feature libraries (which bring `jnirt` and `liblog` transitively):

```cmake
add_library(unified SHARED src/entry.cpp)
target_link_libraries(unified PRIVATE wifi_camera)
```

## Controlling the level at runtime

The level is process-wide and defaults to `Debug`.

```cpp
#include "jnirt/support/logging/log.h"

jnirt::logging::setLevel(jnirt::logging::Level::Info);  // e.g. in release builds
bool on = jnirt::logging::enabled(jnirt::logging::Level::Debug);
```

Call `setLevel` once during initialization. It is an atomic store and is safe to
call at any time.

## Direct API (without macros)

The macros are a convenience layer over one function:

```cpp
jnirt::logging::write(jnirt::logging::Level::Warn, "MyTag",
                        "retry %d of %d", attempt, max);
```

Use it when the tag is not a compile-time constant. It skips the early
level check on argument evaluation, so guard expensive arguments with
`jnirt::logging::enabled(...)`.

## Reading the logs on Android

Logcat is a system-wide buffer. Entries are attributed to your app by PID and
tag. Filtering happens when you read the logs.

```bash
# Everything from your app process
adb logcat --pid=$(adb shell pidof -s com.example.app)

# One tag, Debug and above
adb logcat -s WifiCamera:D

# One tag at Verbose, silence everything else
adb logcat WifiCamera:V '*:S'

# Two tags plus all warnings and errors
adb logcat WifiCamera:D Onvif:D '*:W'

# Clear the buffer, then follow live output
adb logcat -c && adb logcat --pid=$(adb shell pidof -s com.example.app)
```

In Android Studio, open the Logcat tool window and filter by `tag:WifiCamera`
or `package:mine`. Kotlin logs (Timber) and C++ logs appear in the same
stream and are ordered by time, so both sides of a call can be followed together.

## Adding another platform

Only one file is platform-specific. To support a new OS, add a file that defines
`sinkWrite()` from `support/src/logging/log_sink.h` and select it in CMake. The API,
macros, and core do not change.

## Do and don't

- Do define the tag once per file or module and use `LOGx` everywhere else.
- Do log state transitions and failures with enough context (ids, return codes).
- Don't log in tight loops at Info or above. Use Verbose.
- Don't log secrets, credentials, or full network payloads.
- Don't rely on logging for control flow. It may be filtered out.