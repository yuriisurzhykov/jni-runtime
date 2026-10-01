// Not for direct use: include <jnirt/runtime/convert.h>. std::optional<T>: Kotlin's `T?`. See
// docs/adr/0008-type-conversion.md §4.
//
//   std::optional<std::string>   String?
//   std::optional<jint>          Int?       (java.lang.Integer: valueOf / intValue)
//   std::optional<Frame>         Frame?     (your own type)
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_DETAIL_CONVERT_OPTIONAL_H
#define JNIRT_RUNTIME_DETAIL_CONVERT_OPTIONAL_H

#pragma once
#include <jnirt/runtime/detail/convert_core.h>

#include <optional>

namespace jnirt::detail {
    // Boxing a number: Box<jint> is java.lang.Integer. Defined in convert.cpp with the classes of runtime_classes.h.
    template<class J>
    struct Box;

    template<>
    struct Box<jboolean> {
        static constexpr auto descriptor = text("Ljava/lang/Boolean;");

        static LocalRef<jobject> box(JNIEnv *env, jboolean value);

        static jboolean unbox(JNIEnv *env, jobject boxed);
    };

    template<>
    struct Box<jbyte> {
        static constexpr auto descriptor = text("Ljava/lang/Byte;");

        static LocalRef<jobject> box(JNIEnv *env, jbyte value);

        static jbyte unbox(JNIEnv *env, jobject boxed);
    };

    template<>
    struct Box<jchar> {
        static constexpr auto descriptor = text("Ljava/lang/Character;");

        static LocalRef<jobject> box(JNIEnv *env, jchar value);

        static jchar unbox(JNIEnv *env, jobject boxed);
    };

    template<>
    struct Box<jshort> {
        static constexpr auto descriptor = text("Ljava/lang/Short;");

        static LocalRef<jobject> box(JNIEnv *env, jshort value);

        static jshort unbox(JNIEnv *env, jobject boxed);
    };

    template<>
    struct Box<jint> {
        static constexpr auto descriptor = text("Ljava/lang/Integer;");

        static LocalRef<jobject> box(JNIEnv *env, jint value);

        static jint unbox(JNIEnv *env, jobject boxed);
    };

    template<>
    struct Box<jlong> {
        static constexpr auto descriptor = text("Ljava/lang/Long;");

        static LocalRef<jobject> box(JNIEnv *env, jlong value);

        static jlong unbox(JNIEnv *env, jobject boxed);
    };

    template<>
    struct Box<jfloat> {
        static constexpr auto descriptor = text("Ljava/lang/Float;");

        static LocalRef<jobject> box(JNIEnv *env, jfloat value);

        static jfloat unbox(JNIEnv *env, jobject boxed);
    };

    template<>
    struct Box<jdouble> {
        static constexpr auto descriptor = text("Ljava/lang/Double;");

        static LocalRef<jobject> box(JNIEnv *env, jdouble value);

        static jdouble unbox(JNIEnv *env, jobject boxed);
    };

    // The JVM type of an optional: the box of a number, the type itself for a reference.
    template<class T>
    constexpr auto optionalDescriptor() {
        using Inner = RowOf<T>;
        if constexpr (std::is_pointer_v<typename Inner::Jni>) {
            return descriptorText<T>();
        } else {
            return Box<typename Inner::Jni>::descriptor;
        }
    }
} // namespace jnirt::detail

namespace jnirt {
    template<class T>
    struct Convert<std::optional<T> > {
    private:
        using Inner = detail::RowOf<T>;
        // A number crosses as its box (Integer); a reference crosses as itself, null meaning nothing.
        static constexpr bool boxed = !std::is_pointer_v<typename Inner::Jni>;

        static_assert(!std::is_void_v<T>, "jnirt: std::optional<void> has no meaning");
        static_assert(!detail::isHandle<T>,
                      "jnirt: std::optional of a raw JNI reference (jstring, jobject...): the reference is "
                      "already nullable. Use it directly, or std::optional<std::string> for a converted value.");
        static_assert(!detail::NullFlag<Inner>::value, "jnirt: std::optional<std::optional<T>> has no Java meaning");

    public:
        using Jni = std::conditional_t<boxed, jobject, typename Inner::Jni>;
        using Value = std::optional<T>;
        static constexpr bool acceptsNull = true;
        static constexpr auto descriptor = detail::optionalDescriptor<T>();

        static Value fromJava(JNIEnv *env, Jni value) {
            if (value == nullptr) return std::nullopt;
            if constexpr (boxed) {
                return Inner::fromJava(env, detail::Box<typename Inner::Jni>::unbox(env, value));
            } else {
                return Inner::fromJava(env, value);
            }
        }

        static LocalRef<Jni> toJava(JNIEnv *env, const Value &value) {
            if (!value.has_value()) return {};
            if constexpr (boxed) {
                return detail::Box<typename Inner::Jni>::box(env, Inner::toJava(env, *value));
            } else {
                return Inner::toJava(env, *value);
            }
        }
    };
} // namespace jnirt

#endif //JNIRT_RUNTIME_DETAIL_CONVERT_OPTIONAL_H
