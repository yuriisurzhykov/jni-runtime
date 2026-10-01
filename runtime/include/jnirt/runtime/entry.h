// entry(): the wrapper around every native function. No C++ exception ever reaches the JVM.
// See docs/adr/0007-exceptions-entry-and-log-sink.md and docs/learning/exceptions-and-logs-by-example.md.
//
// There is nothing to call. `r.add<CameraJ>().native<&open>("open")` (registry.h) registers a wrapper of `open`
// instead of `open` itself, and the wrapper is, in Kotlin terms, a try/catch around the whole body. It also converts
// the parameters and the result (convert.h), so `open` is written with C++ types:
//
//   jboolean open(JNIEnv *env, jobject thiz, jstring url) {
//       JNIRT_REQUIRE(url != nullptr, "url is null");                        // Kotlin: IllegalArgumentException
//       if (!connect(url)) throw jnirt::Error(Errc::Io, "no route to %s", host);   // Kotlin: NativeException
//       return JNI_TRUE;
//   }
//
// What Kotlin gets (the table is ADR-0002 §5):
//
//   JavaException              the original Throwable, unchanged (a Java method threw, native code did not catch it)
//   IllegalArgument            IllegalArgumentException("text (file.cpp:42)")
//   IllegalState               IllegalStateException("text (file.cpp:42)")
//   jnirt::Error               NativeException(domain, code, "text")
//   std::bad_alloc             OutOfMemoryError
//   any other std::exception   NativeException("jnirt", Internal, "<type>: what()")
//   anything else              NativeException("jnirt", Internal, "unknown C++ exception")
//
// A cause chain made with std::throw_with_nested arrives as Throwable.cause (up to 8 levels). JNIRT_ASSERT and
// JNIRT_FATAL are not exceptions: they crash, as before.
//
// If a Java exception is already pending when a C++ exception is caught (raw JNI code that did not check), the
// pending one wins and the C++ one is only logged.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_RUNTIME_ENTRY_H
#define JNI_RUNTIME_ENTRY_H

#pragma once
#include <jnirt/runtime/convert.h>

#include <jni.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace jnirt::detail {
    // Called from a catch block with the exception being handled: logs it and leaves the matching Java exception
    // pending in `env`. Never throws.
    void handleNativeException(JNIEnv *env) noexcept;

    // The function that JNI calls instead of Func. Its parameters are the JNI types of Func's parameters (jstring for
    // a std::string, jboolean for a bool), so RegisterNatives sees the types Java has. Every parameter is converted
    // inside the try, Func is called with C++ types, and its result is converted back. A failing conversion (a null
    // for a std::string, an out-of-memory) is an exception like any other (ADR-0007, ADR-0008 §8).
    // After an exception it returns 0 / false / nullptr: Java never looks at the value, it sees the exception.
    template<auto Func, class ReturnType, class Self, class... Args>
    struct Entry {
        using JniReturn = JniOf<ReturnType>;

        static JniReturn call(JNIEnv *env, Self self, JniOf<Args>... jni) {
            return run(std::index_sequence_for<Args...>{}, env, self, jni...);
        }

    private:
        template<std::size_t... I>
        static JniReturn run(std::index_sequence<I...>, JNIEnv *env, Self self, JniOf<Args>... jni) {
            try {
                if constexpr (std::is_void_v<ReturnType>) {
                    Func(env, self, readArgument<I, Args>(env, jni)...);
                    return;
                } else {
                    return giveAway(RowOf<ReturnType>::toJava(env, Func(env, self, readArgument<I, Args>(env, jni)...)));
                }
            } catch (...) {
                handleNativeException(env);
            }
            if constexpr (!std::is_void_v<ReturnType>) return JniReturn();
        }
    };
} // namespace jnirt::detail

#endif //JNI_RUNTIME_ENTRY_H
