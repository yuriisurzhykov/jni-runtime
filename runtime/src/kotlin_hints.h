// Runtime-internal: when a method or native is not found at startup, a hint about the usual Kotlin reasons (a mangled
// name, a companion object). Reflection is used only then, so a good startup pays nothing. Not part of the public API.
// See docs/adr/0008-type-conversion.md §9.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_KOTLIN_HINTS_H
#define JNIRT_RUNTIME_KOTLIN_HINTS_H

#pragma once
#include <jni.h>

#include <string>
#include <vector>

namespace jnirt::detail {
    // Pure part. `declared` are the names of the methods the class has. For the wanted name, the Kotlin names that look
    // like it: `wanted-Ab3xQ9z` (a value class or UInt in the signature), `wanted$module` (internal), `wanted$default`
    // (default parameter values). Each hint is on its own line, starting with "\n  hint: "; "" if nothing looks like it.
    std::string mangledNameHint(const std::string &wanted, const std::vector<std::string> &declared);

    // The same for a method that was not found, using reflection on `cls` and on `jvmClassName$Companion`. Never throws
    // and never leaves a Java exception pending; "" when there is nothing to say.
    std::string hintForMissingMethod(JNIEnv *env, jclass cls, const char *jvmClassName, const char *name,
                                     const char *descriptor);
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_KOTLIN_HINTS_H
