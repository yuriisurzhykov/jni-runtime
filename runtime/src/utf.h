// Runtime-internal: UTF-8 and UTF-16 text. The public conversions are in <jnirt/runtime/convert.h> (convert.cpp).
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_UTF_H
#define JNIRT_RUNTIME_UTF_H

#pragma once
#include <jni.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace jnirt::detail {
    // UTF-8 to UTF-16. A byte sequence that is not valid UTF-8 (a stray byte, a cut-off character, an overlong form,
    // a surrogate) becomes U+FFFD, one per bad sequence; nothing throws and nothing is dropped silently.
    std::u16string utf8ToUtf16(std::string_view text);

    // UTF-16 to UTF-8. A surrogate that has no partner becomes U+FFFD; U+0000 stays a zero byte.
    std::string utf16ToUtf8(const char16_t *units, std::size_t count);

    // A new Java string with the text. Standard UTF-8, so an emoji or an invalid byte are safe (NewStringUTF takes
    // modified UTF-8 and CheckJNI aborts on anything else). nullptr with a pending OutOfMemoryError if the JVM
    // could not allocate; the caller decides what to do with it.
    jstring newJavaString(JNIEnv *env, std::string_view text);
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_UTF_H
