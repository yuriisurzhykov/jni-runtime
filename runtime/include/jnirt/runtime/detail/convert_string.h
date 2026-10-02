// Not for direct use: include <jnirt/runtime/convert.h>. Rows for text: std::string (UTF-8), std::u16string (UTF-16),
// std::string_view (to Java only). See docs/adr/0008-type-conversion.md §3.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_DETAIL_CONVERT_STRING_H
#define JNIRT_RUNTIME_DETAIL_CONVERT_STRING_H

#pragma once
#include <jnirt/runtime/detail/convert_core.h>

#include <string>
#include <string_view>

namespace jnirt {
    namespace detail {
        // Java string to UTF-8: GetStringRegion, then UTF-16 to UTF-8. An unpaired surrogate becomes U+FFFD, U+0000
        // stays a zero byte. Defined in convert.cpp.
        std::string readString(JNIEnv *env, jstring text);

        // Java string to UTF-16, as it is.
        std::u16string readString16(JNIEnv *env, jstring text);

        // A new Java string from UTF-8 (invalid bytes become U+FFFD) or UTF-16. An out-of-memory error is thrown as
        // JavaException; the result is never empty.
        LocalRef<jstring> makeString(JNIEnv *env, std::string_view utf8);

        LocalRef<jstring> makeString16(JNIEnv *env, std::u16string_view utf16);
    } // namespace detail

    template<>
    struct Convert<std::string> {
        using Jni = jstring;
        using Value = std::string;
        static constexpr auto descriptor = detail::text("Ljava/lang/String;");

        static std::string fromJava(JNIEnv *env, jstring text) { return detail::readString(env, text); }

        static LocalRef<jstring> toJava(JNIEnv *env, const std::string &text) { return detail::makeString(env, text); }
    };

    template<>
    struct Convert<std::u16string> {
        using Jni = jstring;
        using Value = std::u16string;
        static constexpr auto descriptor = detail::text("Ljava/lang/String;");

        static std::u16string fromJava(JNIEnv *env, jstring text) { return detail::readString16(env, text); }

        static LocalRef<jstring> toJava(JNIEnv *env, const std::u16string &text) {
            return detail::makeString16(env, text);
        }
    };

    // An argument of a call into Java, not a result: the view would point to memory that is gone.
    template<>
    struct Convert<std::string_view> {
        using Jni = jstring;
        using Value = std::string_view;
        static constexpr auto descriptor = detail::text("Ljava/lang/String;");

        template<class T = std::string_view>
        static std::string_view fromJava(JNIEnv *, jstring) {
            static_assert(detail::AlwaysFalse<T>::value,
                          "jnirt: std::string_view can only be given to Java. As a result or a native parameter it "
                          "would point to memory that is gone: use std::string.");
            return {};
        }

        static LocalRef<jstring> toJava(JNIEnv *env, std::string_view text) { return detail::makeString(env, text); }
    };

    // toJava(env, "text"): a literal is a std::string.
    inline LocalRef<jstring> toJava(JNIEnv *env, const char *text) { return detail::makeString(env, text); }
} // namespace jnirt

#endif //JNIRT_RUNTIME_DETAIL_CONVERT_STRING_H
