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
// The types are the rows of the conversion table (convert.h): the JNI types, bool, std::string, std::optional<T>,
// std::vector<T>, and so on. jobject does not compile: it does not say which Java class it is; describe the class
// instead, or use jnirt::Object for a real java.lang.Object.
// uint8_t and uint16_t are the same types as jboolean and jchar; the compiler can not tell them apart.
//
// Both values are plain null-terminated strings that the compiler builds; they cost nothing at run time.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_RUNTIME_SIGNATURE_H
#define JNI_RUNTIME_SIGNATURE_H

#pragma once
#include <jnirt/runtime/convert.h>

#include <jni.h>

namespace jnirt {
    namespace detail {
        // The JVM descriptor of T, as a Text. A parameter written `const std::string &` is std::string.
        template<class T>
        struct Descriptor {
            static constexpr auto value = descriptorText<T>();
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
