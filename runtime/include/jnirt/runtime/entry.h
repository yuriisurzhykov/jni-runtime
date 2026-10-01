// entry(): the wrapper around every native function. No C++ exception ever reaches the JVM.
// See docs/adr/0007-exceptions-entry-and-log-sink.md and docs/learning/exceptions-and-logs-by-example.md.
//
// There is nothing to call. `r.add<CameraJ>().native<&open>("open")` (registry.h) registers a wrapper of `open`
// instead of `open` itself, and the wrapper is, in Kotlin terms, a try/catch around the whole body:
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
#include <jni.h>

#include <type_traits>

namespace jnirt::detail {
    // Called from a catch block with the exception being handled: logs it and leaves the matching Java exception
    // pending in `env`. Never throws.
    void handleNativeException(JNIEnv *env) noexcept;

    // The function that JNI calls instead of Fn. Same parameters as Fn, so RegisterNatives does not notice.
    // After an exception it returns 0 / false / nullptr: Java never looks at the value, it sees the exception.
    template<auto Fn, class R, class Self, class... P>
    R entryCall(JNIEnv *env, Self self, P... args) {
        try {
            if constexpr (std::is_void_v<R>) {
                Fn(env, self, args...);
            } else {
                return Fn(env, self, args...);
            }
        } catch (...) {
            handleNativeException(env);
        }
        if constexpr (!std::is_void_v<R>) return R();
    }
} // namespace jnirt::detail

#endif //JNI_RUNTIME_ENTRY_H
