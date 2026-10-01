// Exception handling/creation in two ways: C++ error <-> Java exception.
//
// This is the half that class_cache needs: a Java exception seen from C++. The other half (C++ error -> Java
// exception) is entry.h.
//
//   try {
//       CameraJ::onFrame(env, camera, 1, name);          // the Java method threw
//   } catch (const jnirt::JavaException &e) {
//       LOGE("onFrame failed: %s", e.what());             // "java.lang.IllegalStateException: no surface"
//   }
//
// Calling Java through class_cache clears the pending Java exception and throws JavaException instead, so the JNI
// rule "no JNI calls with a pending exception" can not be broken by forgetting a check. entry() (entry.h) hands
// throwable() back to Kotlin as it is, so a Java exception that goes through native code arrives unchanged.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_RUNTIME_EXCEPTION_H
#define JNI_RUNTIME_EXCEPTION_H

#pragma once
#include <jnirt/runtime/ref.h>

#include <exception>
#include <memory>
#include <string>
#include <type_traits>

namespace jnirt {
    namespace detail {
        // If a Java exception is pending: clears it and throws JavaException. Not for direct use: the calls of
        // class_cache do this after every call into Java.
        void throwIfPending(JNIEnv *env);

        [[noreturn]] void throwPendingException(JNIEnv *env);
    } // namespace detail

    // A Java exception that was thrown by a call into Java. Holds the Throwable (a global reference, usable on any
    // thread) and its toString(), taken when the exception was caught.
    // Copying is cheap and never throws: copies share one state.
    class JavaException : public std::exception {
    public:
        ~JavaException() override;

        // The original Throwable. Valid while this exception, or a copy of it, is alive.
        jthrowable throwable() const noexcept;

        // Throwable.toString(): "java.lang.IllegalStateException: no surface".
        const char *what() const noexcept override;

    private:
        struct State {
            GlobalRef<jthrowable> throwable;
            std::string text;
        };

        explicit JavaException(std::shared_ptr<const State> state) noexcept;

        friend void detail::throwPendingException(JNIEnv *env);

        std::shared_ptr<const State> state_;
    };

    static_assert(std::is_nothrow_copy_constructible_v<JavaException>, "an exception must be nothrow-copyable");
} // namespace jnirt

#endif //JNI_RUNTIME_EXCEPTION_H
