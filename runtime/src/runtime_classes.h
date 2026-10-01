// The Java classes jnirt itself talks to, described like any user class (class_cache.h): the same lookup, the same
// error messages, the same debug checks. init() finds them before the user's classes (runtimeClasses() in init.cpp).
// Defined in a header, so every file that uses one sees the same ClassInfo.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_RUNTIME_CLASSES_H
#define JNIRT_RUNTIME_RUNTIME_CLASSES_H

#pragma once
#include <jnirt/runtime/class_cache.h>

namespace jnirt::detail {
    // The two members that put a Java exception into words.
    struct ThrowableJ : JavaClass<ThrowableJ> {
        static constexpr const char *name = "java/lang/Throwable";

        static inline Method<ThrowableJ, jstring()> toString{"toString"};
        static inline Method<ThrowableJ, ThrowableJ()> getCause{"getCause"};
    };

    // What entry() throws for a jnirt::Error (android/.../NativeException.kt).
    struct NativeExceptionJ : JavaClass<NativeExceptionJ> {
        static constexpr const char *name = "jnirt/NativeException";

        // (domain, code, message, cause)
        static inline Constructor<NativeExceptionJ, void(jstring, jint, jstring, ThrowableJ)> create;
    };

    struct IllegalArgumentExceptionJ : JavaClass<IllegalArgumentExceptionJ> {
        static constexpr const char *name = "java/lang/IllegalArgumentException";

        static inline Constructor<IllegalArgumentExceptionJ, void(jstring, ThrowableJ)> create;
    };

    struct IllegalStateExceptionJ : JavaClass<IllegalStateExceptionJ> {
        static constexpr const char *name = "java/lang/IllegalStateException";

        static inline Constructor<IllegalStateExceptionJ, void(jstring, ThrowableJ)> create;
    };

    struct OutOfMemoryErrorJ : JavaClass<OutOfMemoryErrorJ> {
        static constexpr const char *name = "java/lang/OutOfMemoryError";

        static inline Constructor<OutOfMemoryErrorJ, void(jstring)> create;
    };

    // Where native log lines go (android/.../NativeLogBridge.kt). `setNativeLevel` is native: init registers it.
    struct NativeLogBridgeJ : JavaClass<NativeLogBridgeJ> {
        static constexpr const char *name = "jnirt/NativeLogBridge";

        // (priority as in android.util.Log, tag, message)
        static inline StaticMethod<NativeLogBridgeJ, void(jint, jstring, jstring)> log{"log"};
    };
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_RUNTIME_CLASSES_H
