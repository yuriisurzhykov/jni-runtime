// Conversion between C++ types and Java types at the border: the table jnirt::Convert<T>, and what is built on it.
// See docs/adr/0008-type-conversion.md and docs/learning/type-conversion-by-example.md.
//
// A native function is written with ordinary C++ types, and `native<&fn>` (registry.h) converts every parameter and the
// result. A call into Java through class_cache.h converts the same way:
//
//   bool open(JNIEnv *env, jobject thiz, const std::string &url, std::optional<jint> port);   // (String, Int?) -> Boolean
//   Method<CameraJ, void(jint, std::string)> onFrame{"onFrame"};  onFrame(env, camera, 1, "text");
//
// By hand: jnirt::fromJava<std::string>(env, jstr) and jnirt::toJava(env, value).
//
// Include this header (or registry.h / class_cache.h, which do) before using a type, and write the row of your own
// type before the first use of it.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_RUNTIME_CONVERT_H
#define JNI_RUNTIME_CONVERT_H

#pragma once
#include <jnirt/runtime/detail/convert_core.h>
#include <jnirt/runtime/detail/convert_string.h>
#include <jnirt/runtime/detail/convert_optional.h>

#endif //JNI_RUNTIME_CONVERT_H
