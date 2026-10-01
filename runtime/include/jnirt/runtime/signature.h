// JVM signatures built at compile time from C++ types. See docs/adr/0005-signatures.md and
// docs/learning/signatures-by-example.md.
//
//   jnirt::descriptor<jlong>                    "J"
//   jnirt::descriptor<jstring>                  "Ljava/lang/String;"
//   jnirt::signature<void(jint, jstring)>       "(ILjava/lang/String;)V"
//   jnirt::signature<jboolean(ListenerJ)>       "(Lcom/acme/camera/Listener;)Z"   with ListenerJ described below
//
// Your own class goes into a signature through a description:
//
//   struct ListenerJ : jnirt::JavaClass<ListenerJ> {
//       static constexpr const char *name = "com/acme/camera/Listener";   // slashes, '$' for nested classes
//   };
//
// Write only JNI types: jboolean (not bool), jchar, jlong (not long long). jobject does not compile: it does not say
// which Java class it is; describe the class instead, or use jnirt::Object for a real java.lang.Object.
// uint8_t and uint16_t are the same types as jboolean and jchar; the compiler can not tell them apart.
//
// Both values are plain null-terminated strings that the compiler builds; they cost nothing at run time.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_RUNTIME_SIGNATURE_H
#define JNI_RUNTIME_SIGNATURE_H

#pragma once
#include <jni.h>

#include <cstddef>
#include <type_traits>

namespace jnirt {
    // Base of a Java class description: struct CameraJ : JavaClass<CameraJ> { static constexpr const char *name = ...; }.
    // Defined in class_cache.h (P3.5); here it is only what makes CameraJ usable in a signature.
    template<class Self>
    struct JavaClass;

    namespace detail {
        // A string whose length N (without the terminating '\0') is part of its type, so the compiler can build it.
        template<std::size_t N>
        struct Text {
            char chars[N + 1]{};

            constexpr const char *c_str() const { return chars; }
        };

        // "abc" -> Text<3>
        template<std::size_t N>
        constexpr Text<N - 1> text(const char (&literal)[N]) {
            Text<N - 1> result{};
            for (std::size_t i = 0; i < N - 1; ++i) result.chars[i] = literal[i];
            return result;
        }

        // Text<A> + Text<B> -> Text<A + B>
        template<std::size_t A, std::size_t B>
        constexpr Text<A + B> operator+(const Text<A> &left, const Text<B> &right) {
            Text<A + B> result{};
            for (std::size_t i = 0; i < A; ++i) result.chars[i] = left.chars[i];
            for (std::size_t i = 0; i < B; ++i) result.chars[A + i] = right.chars[i];
            return result;
        }

        constexpr std::size_t length(const char *s) {
            std::size_t n = 0;
            while (s[n] != '\0') ++n;
            return n;
        }

        // A class name in JVM form: "com/acme/Camera", "com/acme/Camera$State".
        constexpr bool isJvmClassName(const char *name) {
            if (name[0] == '\0' || name[0] == '[') return false;
            for (std::size_t i = 0; name[i] != '\0'; ++i) {
                if (name[i] == '.' || name[i] == ';') return false;
            }
            return true;
        }

        // false, but only once T is known: lets a static_assert fire only for the type somebody actually used.
        template<class T>
        struct AlwaysFalse : std::false_type {
        };

        template<class T>
        constexpr bool isJavaClass = std::is_base_of_v<JavaClass<T>, T>;

        // The type table: Descriptor<T>::value is the JVM descriptor of T. This primary template is the "no such
        // row" case; the rows are the specializations below.
        template<class T, class = void>
        struct Descriptor {
            static_assert(!std::is_same_v<T, jobject>,
                          "jnirt: jobject does not say which Java class. Describe the class "
                          "(struct ListenerJ : jnirt::JavaClass<ListenerJ>) and use ListenerJ; "
                          "for a real java.lang.Object use jnirt::Object.");
            static_assert(!std::is_same_v<T, bool>, "jnirt: use jboolean, not bool");
            static_assert(std::is_same_v<T, jobject> || std::is_same_v<T, bool> || AlwaysFalse<T>::value,
                          "jnirt: this C++ type has no JVM descriptor. Supported: void, jboolean, jbyte, jchar, "
                          "jshort, jint, jlong, jfloat, jdouble, jstring, jclass, jthrowable, j<primitive>Array, "
                          "and classes described with jnirt::JavaClass.");
        };

        template<>
        struct Descriptor<void> {
            static constexpr auto value = text("V");
        };

        template<>
        struct Descriptor<jboolean> {
            static constexpr auto value = text("Z");
        };

        template<>
        struct Descriptor<jbyte> {
            static constexpr auto value = text("B");
        };

        template<>
        struct Descriptor<jchar> {
            static constexpr auto value = text("C");
        };

        template<>
        struct Descriptor<jshort> {
            static constexpr auto value = text("S");
        };

        template<>
        struct Descriptor<jint> {
            static constexpr auto value = text("I");
        };

        template<>
        struct Descriptor<jlong> {
            static constexpr auto value = text("J");
        };

        template<>
        struct Descriptor<jfloat> {
            static constexpr auto value = text("F");
        };

        template<>
        struct Descriptor<jdouble> {
            static constexpr auto value = text("D");
        };

        template<>
        struct Descriptor<jstring> {
            static constexpr auto value = text("Ljava/lang/String;");
        };

        template<>
        struct Descriptor<jclass> {
            static constexpr auto value = text("Ljava/lang/Class;");
        };

        template<>
        struct Descriptor<jthrowable> {
            static constexpr auto value = text("Ljava/lang/Throwable;");
        };

        template<>
        struct Descriptor<jbooleanArray> {
            static constexpr auto value = text("[Z");
        };

        template<>
        struct Descriptor<jbyteArray> {
            static constexpr auto value = text("[B");
        };

        template<>
        struct Descriptor<jcharArray> {
            static constexpr auto value = text("[C");
        };

        template<>
        struct Descriptor<jshortArray> {
            static constexpr auto value = text("[S");
        };

        template<>
        struct Descriptor<jintArray> {
            static constexpr auto value = text("[I");
        };

        template<>
        struct Descriptor<jlongArray> {
            static constexpr auto value = text("[J");
        };

        template<>
        struct Descriptor<jfloatArray> {
            static constexpr auto value = text("[F");
        };

        template<>
        struct Descriptor<jdoubleArray> {
            static constexpr auto value = text("[D");
        };

        // C::name copied into a Text, so it can be glued to "L" and ";".
        template<class C>
        constexpr auto classNameText() {
            constexpr std::size_t n = length(C::name);
            Text<n> result{};
            for (std::size_t i = 0; i < n; ++i) result.chars[i] = C::name[i];
            return result;
        }

        // The last row: any class described with JavaClass<C>.
        template<class C>
        struct Descriptor<C, std::enable_if_t<isJavaClass<C> > > {
            static_assert(isJvmClassName(C::name),
                          "jnirt: JavaClass name must be in JVM form: \"com/acme/Camera\" (slashes, '$' for nested "
                          "classes), not \"com.acme.Camera\" and not \"Lcom/acme/Camera;\"");
            static constexpr auto value = text("L") + classNameText<C>() + text(";");
        };

        template<class F>
        struct Signature {
            static_assert(AlwaysFalse<F>::value, "jnirt: signature<F> needs a function type, like void(jint, jstring)");
        };

        // "(" + descriptors of the parameters + ")" + descriptor of the result
        template<class R, class... Params>
        struct Signature<R(Params...)> {
            static constexpr auto value = (text("(") + ... + Descriptor<Params>::value) + text(")") + Descriptor<
                                              R>::value;
        };
    } // namespace detail

    // The JVM descriptor of one type: for a field (GetFieldID) or as part of a signature.
    template<class T>
    constexpr const char *descriptor = detail::Descriptor<T>::value.c_str();

    // The JVM signature of a method with the given C++ function type: for GetMethodID and RegisterNatives.
    template<class F>
    constexpr const char *signature = detail::Signature<F>::value.c_str();
} // namespace jnirt

#endif //JNI_RUNTIME_SIGNATURE_H
