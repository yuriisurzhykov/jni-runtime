// Not for direct use: include <jnirt/runtime/convert.h>. The table jnirt::Convert<T> itself, its rows for numbers,
// bool and raw JNI references, and the helpers every other row and the call machinery use. See
// docs/adr/0008-type-conversion.md.
//
// A row says, for a C++ type T:
//
//   using Jni   = ...;                    what crosses the border: jint, jstring, jobject, ...
//   using Value = ...;                    what C++ code holds: the type itself for T
//   static constexpr auto descriptor      the JVM descriptor, built at compile time ("Ljava/lang/String;")
//   static Value fromJava(JNIEnv *, Jni)                       a Java value, read (never given a null if Jni is a
//                                                              reference and the row does not set acceptsNull)
//   static ... toJava(JNIEnv *, const Value &)                 Jni for a number, LocalRef<Jni> for a reference
//   static constexpr bool handle      (optional) true: the value is a raw JNI reference, not converted
//   static constexpr bool acceptsNull (optional) true: fromJava gets null (std::optional)
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_DETAIL_CONVERT_CORE_H
#define JNIRT_RUNTIME_DETAIL_CONVERT_CORE_H

#pragma once
#include <jnirt/runtime/detail/text.h>
#include <jnirt/runtime/ref.h>

#include <jni.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace jnirt {
    // Base of a Java class description: struct CameraJ : JavaClass<CameraJ> { static constexpr const char *name = ...; }.
    // Defined in class_cache.h; here it is only what makes CameraJ usable in a signature or a conversion.
    template<class Self>
    struct JavaClass;

    namespace detail {
        // The type without const and reference: what the table is keyed by.
        template<class T>
        using Plain = std::remove_cv_t<std::remove_reference_t<T>>;

        template<class T>
        constexpr bool isJavaClass = std::is_base_of_v<JavaClass<T>, T>;

        // A class name in JVM form: "com/acme/Camera", "com/acme/Camera$State".
        constexpr bool isJvmClassName(const char *name) {
            if (name[0] == '\0' || name[0] == '[') return false;
            for (std::size_t i = 0; name[i] != '\0'; ++i) {
                if (name[i] == '.' || name[i] == ';') return false;
            }
            return true;
        }

        // C::name copied into a Text, so it can be glued to "L" and ";".
        template<class C>
        constexpr auto classNameText() {
            constexpr std::size_t n = length(C::name);
            Text<n> result{};
            for (std::size_t i = 0; i < n; ++i) result.chars[i] = C::name[i];
            return result;
        }

        // "Lcom/acme/Camera;" for a described class.
        template<class C>
        constexpr auto classDescriptor() {
            static_assert(isJvmClassName(C::name),
                          "jnirt: JavaClass name must be in JVM form: \"com/acme/Camera\" (slashes, '$' for nested "
                          "classes), not \"com.acme.Camera\" and not \"Lcom/acme/Camera;\"");
            return text("L") + classNameText<C>() + text(";");
        }

        // What a row looks like to the machinery: optional members have defaults.
        template<class Row, class = void>
        struct HandleFlag : std::false_type {
        };

        template<class Row>
        struct HandleFlag<Row, std::void_t<decltype(Row::handle)> > : std::bool_constant<Row::handle> {
        };

        template<class Row, class = void>
        struct NullFlag : std::false_type {
        };

        template<class Row>
        struct NullFlag<Row, std::void_t<decltype(Row::acceptsNull)> > : std::bool_constant<Row::acceptsNull> {
        };

        template<class Row, class = void>
        struct HasDescriptor : std::false_type {
        };

        template<class Row>
        struct HasDescriptor<Row, std::void_t<decltype(Row::descriptor)> > : std::true_type {
        };

        // A number or raw reference that is its own Jni and Value: nothing is converted.
        template<class T>
        struct PrimitiveRow {
            using Jni = T;
            using Value = T;

            static constexpr T fromJava(JNIEnv *, T value) noexcept { return value; }

            static constexpr T toJava(JNIEnv *, T value) noexcept { return value; }
        };

        // A raw JNI reference (jstring, jintArray, a described class): the value is the reference itself. A native
        // function gets the JVM's reference and does not own it; a call into Java gives a LocalRef (class_cache.h).
        template<class J>
        struct HandleRow {
            using Jni = J;
            using Value = J;
            static constexpr bool handle = true;

            static J fromJava(JNIEnv *, J value) noexcept { return value; }

            static J toJava(JNIEnv *, J value) noexcept { return value; }
        };
    } // namespace detail

    // The table. Specialize it for your own type (see ConvertObject below); the rows of jnirt are in the headers
    // that convert.h includes. This is the "no such row" case.
    template<class T, class Enable = void>
    struct Convert {
        static constexpr bool isUnsigned = std::is_integral_v<T> && std::is_unsigned_v<T>;
        static constexpr bool isOtherInteger = std::is_integral_v<T> && !std::is_unsigned_v<T>;

        static_assert(!isUnsigned,
                      "jnirt: Java has no unsigned numbers (size_t, uint32_t, uint64_t, unsigned). Take jint / jlong "
                      "and check the range yourself.");
        static_assert(!isOtherInteger,
                      "jnirt: this integer type is not one of the JNI types. Write jint, jlong, jshort, jbyte; "
                      "'long long' is not jlong on a 64-bit ABI, and 'char' is neither jbyte nor jchar.");
        static_assert(isUnsigned || isOtherInteger || detail::AlwaysFalse<T>::value,
                      "jnirt: this C++ type has no Java type. Supported: void, bool, the JNI numbers (jint, jlong, "
                      "...), std::string, std::u16string, std::optional<T>, std::vector<T>, std::map<K, V>, "
                      "std::unordered_map<K, V>, jnirt::DirectBuffer, jnirt::Array<C>, jstring, jclass, jthrowable, "
                      "j<primitive>Array, and classes described with jnirt::JavaClass. For your own type add a row: "
                      "template<> struct jnirt::Convert<MyType> : jnirt::ConvertObject<MyType, MyTypeJ> { ... }");

        // Only so that one mistake gives one error, not a second one about the descriptor.
        static constexpr auto descriptor = detail::text("?");
    };

    // ---- void and the numbers ----

    template<>
    struct Convert<void> {
        using Jni = void;
        using Value = void;
        static constexpr auto descriptor = detail::text("V");
    };

    template<>
    struct Convert<jboolean> : detail::PrimitiveRow<jboolean> {
        static constexpr auto descriptor = detail::text("Z");
    };

    template<>
    struct Convert<jbyte> : detail::PrimitiveRow<jbyte> {
        static constexpr auto descriptor = detail::text("B");
    };

    template<>
    struct Convert<jchar> : detail::PrimitiveRow<jchar> {
        static constexpr auto descriptor = detail::text("C");
    };

    template<>
    struct Convert<jshort> : detail::PrimitiveRow<jshort> {
        static constexpr auto descriptor = detail::text("S");
    };

    template<>
    struct Convert<jint> : detail::PrimitiveRow<jint> {
        static constexpr auto descriptor = detail::text("I");
    };

    template<>
    struct Convert<jlong> : detail::PrimitiveRow<jlong> {
        static constexpr auto descriptor = detail::text("J");
    };

    template<>
    struct Convert<jfloat> : detail::PrimitiveRow<jfloat> {
        static constexpr auto descriptor = detail::text("F");
    };

    template<>
    struct Convert<jdouble> : detail::PrimitiveRow<jdouble> {
        static constexpr auto descriptor = detail::text("D");
    };

    // bool is a Boolean; jboolean stays as the raw type.
    template<>
    struct Convert<bool> {
        using Jni = jboolean;
        using Value = bool;
        static constexpr auto descriptor = detail::text("Z");

        static bool fromJava(JNIEnv *, jboolean value) noexcept { return value != JNI_FALSE; }

        static jboolean toJava(JNIEnv *, bool value) noexcept { return value ? JNI_TRUE : JNI_FALSE; }
    };

    // ---- raw JNI references ----

    template<>
    struct Convert<jstring> : detail::HandleRow<jstring> {
        static constexpr auto descriptor = detail::text("Ljava/lang/String;");
    };

    template<>
    struct Convert<jclass> : detail::HandleRow<jclass> {
        static constexpr auto descriptor = detail::text("Ljava/lang/Class;");
    };

    template<>
    struct Convert<jthrowable> : detail::HandleRow<jthrowable> {
        static constexpr auto descriptor = detail::text("Ljava/lang/Throwable;");
    };

    template<>
    struct Convert<jbooleanArray> : detail::HandleRow<jbooleanArray> {
        static constexpr auto descriptor = detail::text("[Z");
    };

    template<>
    struct Convert<jbyteArray> : detail::HandleRow<jbyteArray> {
        static constexpr auto descriptor = detail::text("[B");
    };

    template<>
    struct Convert<jcharArray> : detail::HandleRow<jcharArray> {
        static constexpr auto descriptor = detail::text("[C");
    };

    template<>
    struct Convert<jshortArray> : detail::HandleRow<jshortArray> {
        static constexpr auto descriptor = detail::text("[S");
    };

    template<>
    struct Convert<jintArray> : detail::HandleRow<jintArray> {
        static constexpr auto descriptor = detail::text("[I");
    };

    template<>
    struct Convert<jlongArray> : detail::HandleRow<jlongArray> {
        static constexpr auto descriptor = detail::text("[J");
    };

    template<>
    struct Convert<jfloatArray> : detail::HandleRow<jfloatArray> {
        static constexpr auto descriptor = detail::text("[F");
    };

    template<>
    struct Convert<jdoubleArray> : detail::HandleRow<jdoubleArray> {
        static constexpr auto descriptor = detail::text("[D");
    };

    // jobject and jobjectArray can be passed through (a native with an explicit Java signature takes a jobject), but
    // they do not say which Java class they are, so they have no descriptor.
    template<>
    struct Convert<jobject> : detail::HandleRow<jobject> {
    };

    template<>
    struct Convert<jobjectArray> : detail::HandleRow<jobjectArray> {
    };

    // A class described with jnirt::JavaClass, in a signature or as a parameter of a native with an explicit Java
    // signature. The C++ side is a jobject.
    template<class C>
    struct Convert<C, std::enable_if_t<detail::isJavaClass<C> > > : detail::HandleRow<jobject> {
        static constexpr auto descriptor = detail::classDescriptor<C>();
    };

    // The base of the row for your own type: `Cpp` is the C++ struct, `C` the description of its Java class.
    //
    //   template<> struct jnirt::Convert<Frame> : jnirt::ConvertObject<Frame, FrameJ> {
    //       static Frame fromJava(JNIEnv *env, jobject f);
    //       static LocalRef<jobject> toJava(JNIEnv *env, const Frame &f);
    //   };
    //
    // fromJava never gets null (a null is an IllegalArgumentException before it); write it for a real object.
    template<class Cpp, class C>
    struct ConvertObject {
        static_assert(detail::isJavaClass<C>,
                      "jnirt: ConvertObject<Cpp, C>: C must be described as struct C : jnirt::JavaClass<C> "
                      "{ static constexpr const char *name = \"com/acme/Frame\"; }");

        using Jni = jobject;
        using Value = Cpp;
        static constexpr auto descriptor = detail::classDescriptor<C>();
    };

    namespace detail {
        template<class T>
        using RowOf = Convert<Plain<T> >;

        template<class T>
        using JniOf = typename RowOf<T>::Jni;

        template<class T>
        using ValueOf = typename RowOf<T>::Value;

        template<class T>
        constexpr bool isHandle = HandleFlag<RowOf<T> >::value;

        // The row crosses a reference, and C++ has no null for its value: a null from Java is an error.
        template<class T>
        constexpr bool rejectsNull = std::is_pointer_v<JniOf<T> > && !isHandle<T> && !NullFlag<RowOf<T> >::value;

        // How a value of type T is written when it is given to a call: numbers and pointers by value, the rest by
        // const reference.
        template<class T>
        using ArgOf = std::conditional_t<std::is_scalar_v<ValueOf<T> >, ValueOf<T>, const ValueOf<T> &>;

        // The type of the JNI call for T: jobject for every reference, the number itself otherwise.
        template<class T>
        using CallType = std::conditional_t<std::is_pointer_v<JniOf<T> >, jobject, JniOf<T> >;

        // The JVM descriptor of T. A row without a descriptor gets a message of its own.
        template<class T>
        constexpr auto descriptorText() {
            using Row = RowOf<T>;
            if constexpr (HasDescriptor<Row>::value) {
                return Row::descriptor;
            } else if constexpr (std::is_same_v<Plain<T>, jobject>) {
                static_assert(AlwaysFalse<T>::value,
                              "jnirt: jobject does not say which Java class. Describe the class "
                              "(struct ListenerJ : jnirt::JavaClass<ListenerJ>) and use ListenerJ; "
                              "for a real java.lang.Object use jnirt::Object.");
                return text("");
            } else if constexpr (std::is_same_v<Plain<T>, jobjectArray>) {
                static_assert(AlwaysFalse<T>::value,
                              "jnirt: jobjectArray does not say the element class. Use jnirt::Array<FrameJ>.");
                return text("");
            } else {
                static_assert(AlwaysFalse<T>::value,
                              "jnirt: the row of this type has no `descriptor` (the JVM descriptor, built with "
                              "jnirt::detail::text or inherited from jnirt::ConvertObject).");
                return text("");
            }
        }

        // What a conversion hands to JNI: a number as it is, or the reference of a LocalRef (which stays owned by it).
        template<class J, std::enable_if_t<std::is_arithmetic_v<J> || std::is_pointer_v<J>, int> = 0>
        constexpr J rawOf(J value) noexcept {
            return value;
        }

        template<class J>
        J rawOf(const LocalRef<J> &ref) noexcept {
            return ref.get();
        }

        // What a native function returns to the JVM: a number as it is, or a local reference, given up.
        template<class J, std::enable_if_t<std::is_arithmetic_v<J> || std::is_pointer_v<J>, int> = 0>
        constexpr J giveAway(J value) noexcept {
            return value;
        }

        template<class J>
        J giveAway(LocalRef<J> &&ref) noexcept {
            return ref.release();
        }

        // A null where the C++ type has none: IllegalArgumentException. Defined in convert.cpp. `descriptor` is the
        // JVM type that was expected, for the message.
        [[noreturn]] void nullArgument(std::size_t number, const char *descriptor);

        [[noreturn]] void nullValue(const char *descriptor);

        [[noreturn]] void nullElement(std::size_t index, const char *descriptor);

        // The parameter number `I` (from 0) of a native function, read from what JNI gave: null is checked here, so
        // the rows never see one.
        template<std::size_t I, class P>
        ValueOf<P> readArgument(JNIEnv *env, JniOf<P> jni) {
            if constexpr (rejectsNull<P>) {
                if (jni == nullptr) nullArgument(I + 1, RowOf<P>::descriptor.c_str());
            }
            return RowOf<P>::fromJava(env, jni);
        }
    } // namespace detail

    // The Java value `value` as a C++ value: jnirt::fromJava<std::string>(env, jstr). A null for a type that has no
    // null (std::string, your own struct) is an IllegalArgumentException; std::optional<T> takes it.
    template<class T>
    detail::ValueOf<T> fromJava(JNIEnv *env, detail::JniOf<T> value) {
        if constexpr (detail::rejectsNull<T>) {
            if (value == nullptr) detail::nullValue(detail::RowOf<T>::descriptor.c_str());
        }
        return detail::RowOf<T>::fromJava(env, value);
    }

    // A C++ value as a Java one: jnirt::toJava(env, std::string("text")). A number comes back as it is, an object as
    // a LocalRef (`release()` it to give it to Java).
    template<class T>
    auto toJava(JNIEnv *env, const T &value) {
        return detail::RowOf<T>::toJava(env, value);
    }
} // namespace jnirt

#endif //JNIRT_RUNTIME_DETAIL_CONVERT_CORE_H
