// Not for direct use: include <jnirt/runtime/convert.h>. Arrays of primitives and large data: std::vector<jint> <->
// IntArray (a copy), readArray / writeArray (a piece), withCritical (no copy), DirectBuffer (a direct ByteBuffer).
// See docs/adr/0008-type-conversion.md §5 and §6.
//
//   std::vector<jint> doubled(JNIEnv *env, jobject thiz, std::vector<jint> in);       // IntArray -> IntArray
//   jlong sum(JNIEnv *env, jobject thiz, jintArray ints) {                             // no copy, one pass
//       return jnirt::withCriticalRead(env, ints, [](const jint *p, std::size_t n) {
//           jlong total = 0;
//           for (std::size_t i = 0; i < n; ++i) total += p[i];
//           return total;
//       });
//   }
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_DETAIL_CONVERT_ARRAY_H
#define JNIRT_RUNTIME_DETAIL_CONVERT_ARRAY_H

#pragma once
#include <jnirt/runtime/detail/convert_core.h>
#include <jnirt/runtime/exception.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace jnirt {
    namespace detail {
        // The eight JNI array types: the Java name, the C++ element and the three JNI calls for each. Overloads and
        // specializations, no macros: the compiler checks every line.
        template<class A>
        struct ArrayTraits {
            static constexpr bool isArray = false;
            using Element = char; // only so that a wrong type gives the one message of checkBuffer
            static constexpr auto descriptor = text("?");
        };

        template<>
        struct ArrayTraits<jbooleanArray> {
            static constexpr bool isArray = true;
            using Element = jboolean;
            static constexpr auto descriptor = text("[Z");

            static jbooleanArray make(JNIEnv *env, jsize size) { return env->NewBooleanArray(size); }

            static void get(JNIEnv *env, jbooleanArray a, jsize start, jsize count, jboolean *dst) {
                env->GetBooleanArrayRegion(a, start, count, dst);
            }

            static void set(JNIEnv *env, jbooleanArray a, jsize start, jsize count, const jboolean *src) {
                env->SetBooleanArrayRegion(a, start, count, src);
            }
        };

        template<>
        struct ArrayTraits<jbyteArray> {
            static constexpr bool isArray = true;
            using Element = jbyte;
            static constexpr auto descriptor = text("[B");

            static jbyteArray make(JNIEnv *env, jsize size) { return env->NewByteArray(size); }

            static void get(JNIEnv *env, jbyteArray a, jsize start, jsize count, jbyte *dst) {
                env->GetByteArrayRegion(a, start, count, dst);
            }

            static void set(JNIEnv *env, jbyteArray a, jsize start, jsize count, const jbyte *src) {
                env->SetByteArrayRegion(a, start, count, src);
            }
        };

        template<>
        struct ArrayTraits<jcharArray> {
            static constexpr bool isArray = true;
            using Element = jchar;
            static constexpr auto descriptor = text("[C");

            static jcharArray make(JNIEnv *env, jsize size) { return env->NewCharArray(size); }

            static void get(JNIEnv *env, jcharArray a, jsize start, jsize count, jchar *dst) {
                env->GetCharArrayRegion(a, start, count, dst);
            }

            static void set(JNIEnv *env, jcharArray a, jsize start, jsize count, const jchar *src) {
                env->SetCharArrayRegion(a, start, count, src);
            }
        };

        template<>
        struct ArrayTraits<jshortArray> {
            static constexpr bool isArray = true;
            using Element = jshort;
            static constexpr auto descriptor = text("[S");

            static jshortArray make(JNIEnv *env, jsize size) { return env->NewShortArray(size); }

            static void get(JNIEnv *env, jshortArray a, jsize start, jsize count, jshort *dst) {
                env->GetShortArrayRegion(a, start, count, dst);
            }

            static void set(JNIEnv *env, jshortArray a, jsize start, jsize count, const jshort *src) {
                env->SetShortArrayRegion(a, start, count, src);
            }
        };

        template<>
        struct ArrayTraits<jintArray> {
            static constexpr bool isArray = true;
            using Element = jint;
            static constexpr auto descriptor = text("[I");

            static jintArray make(JNIEnv *env, jsize size) { return env->NewIntArray(size); }

            static void get(JNIEnv *env, jintArray a, jsize start, jsize count, jint *dst) {
                env->GetIntArrayRegion(a, start, count, dst);
            }

            static void set(JNIEnv *env, jintArray a, jsize start, jsize count, const jint *src) {
                env->SetIntArrayRegion(a, start, count, src);
            }
        };

        template<>
        struct ArrayTraits<jlongArray> {
            static constexpr bool isArray = true;
            using Element = jlong;
            static constexpr auto descriptor = text("[J");

            static jlongArray make(JNIEnv *env, jsize size) { return env->NewLongArray(size); }

            static void get(JNIEnv *env, jlongArray a, jsize start, jsize count, jlong *dst) {
                env->GetLongArrayRegion(a, start, count, dst);
            }

            static void set(JNIEnv *env, jlongArray a, jsize start, jsize count, const jlong *src) {
                env->SetLongArrayRegion(a, start, count, src);
            }
        };

        template<>
        struct ArrayTraits<jfloatArray> {
            static constexpr bool isArray = true;
            using Element = jfloat;
            static constexpr auto descriptor = text("[F");

            static jfloatArray make(JNIEnv *env, jsize size) { return env->NewFloatArray(size); }

            static void get(JNIEnv *env, jfloatArray a, jsize start, jsize count, jfloat *dst) {
                env->GetFloatArrayRegion(a, start, count, dst);
            }

            static void set(JNIEnv *env, jfloatArray a, jsize start, jsize count, const jfloat *src) {
                env->SetFloatArrayRegion(a, start, count, src);
            }
        };

        template<>
        struct ArrayTraits<jdoubleArray> {
            static constexpr bool isArray = true;
            using Element = jdouble;
            static constexpr auto descriptor = text("[D");

            static jdoubleArray make(JNIEnv *env, jsize size) { return env->NewDoubleArray(size); }

            static void get(JNIEnv *env, jdoubleArray a, jsize start, jsize count, jdouble *dst) {
                env->GetDoubleArrayRegion(a, start, count, dst);
            }

            static void set(JNIEnv *env, jdoubleArray a, jsize start, jsize count, const jdouble *src) {
                env->SetDoubleArrayRegion(a, start, count, src);
            }
        };

        // The array type of a C++ element of std::vector<E>; void for a type that has none. `jboolean` (which is
        // `unsigned char`, the same type as uint8_t) is left out on purpose: see the row of std::vector below.
        template<class E>
        struct ArrayOfElement {
            using Array = void;
        };

        template<>
        struct ArrayOfElement<jbyte> {
            using Array = jbyteArray;
        };

        template<>
        struct ArrayOfElement<std::byte> {
            using Array = jbyteArray;
        };

        template<>
        struct ArrayOfElement<jchar> {
            using Array = jcharArray;
        };

        template<>
        struct ArrayOfElement<jshort> {
            using Array = jshortArray;
        };

        template<>
        struct ArrayOfElement<jint> {
            using Array = jintArray;
        };

        template<>
        struct ArrayOfElement<jlong> {
            using Array = jlongArray;
        };

        template<>
        struct ArrayOfElement<jfloat> {
            using Array = jfloatArray;
        };

        template<>
        struct ArrayOfElement<jdouble> {
            using Array = jdoubleArray;
        };

        // std::vector<E> is an array of numbers (bool and jboolean have rows of their own).
        template<class E>
        constexpr bool isNumberElement = !std::is_void_v<typename ArrayOfElement<E>::Array>;

        // May a buffer of E hold the elements of the JNI array A? The array type says what Java has, so a
        // uint8_t buffer is fine for a ByteArray (it can not be taken for a BooleanArray here).
        template<class A, class E>
        constexpr bool holds = std::is_same_v<E, typename ArrayTraits<A>::Element> ||
                               (std::is_same_v<A, jbyteArray> && (std::is_same_v<E, std::byte> ||
                                                                   std::is_same_v<E, unsigned char>)) ||
                               (std::is_same_v<A, jbooleanArray> && std::is_same_v<E, bool>) ||
                               (std::is_same_v<A, jcharArray> && std::is_same_v<E, char16_t>);

        static_assert(sizeof(bool) == sizeof(jboolean), "jnirt: a bool buffer is read and written as jboolean");

        template<class A, class E>
        constexpr void checkBuffer() {
            static_assert(ArrayTraits<A>::isArray,
                          "jnirt: this needs a JNI array of primitives (jbooleanArray, jbyteArray, jcharArray, "
                          "jshortArray, jintArray, jlongArray, jfloatArray, jdoubleArray)");
            static_assert(holds<A, E>,
                          "jnirt: the buffer does not match the array: jintArray with jint, jbyteArray with jbyte / "
                          "std::byte / uint8_t, jbooleanArray with jboolean / bool, jcharArray with jchar, and so on.");
        }

        // A Java array held by GetPrimitiveArrayCritical, released in the destructor, also when the lambda throws.
        // `readOnly`: the release does not copy back (JNI_ABORT). While one exists the thread is "inside critical":
        // no JNI call, no waiting, nothing that may take a lock. The log forwarder declines then (the line goes to
        // logcat), and debug builds assert in env() and in every call of class_cache.
        class CriticalArray {
        public:
            CriticalArray(JNIEnv *env, jarray array, bool readOnly);

            ~CriticalArray();

            CriticalArray(const CriticalArray &) = delete;

            CriticalArray &operator=(const CriticalArray &) = delete;

            void *data() const noexcept { return data_; }

            jsize size() const noexcept { return size_; }

        private:
            JNIEnv *env_;
            jarray array_;
            void *data_;
            jsize size_;
            bool readOnly_;
        };

        // A Java array of this many elements, or an IllegalArgument if the size does not fit a Java array.
        jsize arraySize(std::size_t count);

        void requireArray(jarray array);
    } // namespace detail

    // The length of any Java array.
    inline jsize arrayLength(JNIEnv *env, jarray array) {
        detail::requireArray(array);
        return env->GetArrayLength(array);
    }

    // Copies [start, start + count) of a Java array into `dst` (which must have room for `count`). A range outside the
    // array is the JVM's ArrayIndexOutOfBoundsException, thrown as JavaException.
    template<class A, class E>
    void readArray(JNIEnv *env, A array, jsize start, E *dst, jsize count) {
        detail::checkBuffer<A, E>();
        detail::requireArray(array);
        detail::ArrayTraits<A>::get(env, array, start, count,
                                    reinterpret_cast<typename detail::ArrayTraits<A>::Element *>(dst));
        detail::throwIfPending(env);
    }

    // Copies `count` elements of `src` into a Java array, from index `start`.
    template<class A, class E>
    void writeArray(JNIEnv *env, A array, jsize start, const E *src, jsize count) {
        detail::checkBuffer<A, E>();
        detail::requireArray(array);
        detail::ArrayTraits<A>::set(env, array, start, count,
                                    reinterpret_cast<const typename detail::ArrayTraits<A>::Element *>(src));
        detail::throwIfPending(env);
    }

    // One pass over the elements of a Java array without a copy (GetPrimitiveArrayCritical). fn(E *data, size_t size)
    // is called with the elements, its changes are written back. The array is released when fn returns or throws.
    //
    // Inside fn: no JNI, no jnirt::env(), no call into Java, no waiting for another thread, no long work: the garbage
    // collector may be held up for as long as fn runs. fn gets no env so that JNI is not called by accident; debug
    // builds assert if jnirt::env() or a class_cache call is made. A log line goes to logcat, not to Java.
    template<class A, class F>
    decltype(auto) withCritical(JNIEnv *env, A array, F &&fn) {
        static_assert(detail::ArrayTraits<A>::isArray, "jnirt: withCritical needs a JNI array of primitives");
        detail::CriticalArray held(env, array, false);
        return fn(static_cast<typename detail::ArrayTraits<A>::Element *>(held.data()),
                  static_cast<std::size_t>(held.size()));
    }

    // The same, read only: fn gets `const E *` and nothing is copied back.
    template<class A, class F>
    decltype(auto) withCriticalRead(JNIEnv *env, A array, F &&fn) {
        static_assert(detail::ArrayTraits<A>::isArray, "jnirt: withCriticalRead needs a JNI array of primitives");
        detail::CriticalArray held(env, array, true);
        return fn(static_cast<const typename detail::ArrayTraits<A>::Element *>(held.data()),
                  static_cast<std::size_t>(held.size()));
    }

    // Memory of a direct java.nio.ByteBuffer: a pointer and the capacity. It does not own the memory; Kotlin owns the
    // buffer and the memory lives as long as the buffer does. Only from Java to C++, and only as a native parameter:
    // the result of a call into Java (or a field) could be a buffer that nothing else holds, and it would be collected
    // under your feet. To read one of those, keep the reference and ask by hand: jnirt::fromJava<DirectBuffer>(env, ref.get()).
    class DirectBuffer {
    public:
        DirectBuffer() noexcept = default;

        DirectBuffer(std::byte *data, std::size_t size) noexcept : data_(data), size_(size) {}

        std::byte *data() const noexcept { return data_; }

        // The capacity of the buffer (position and limit are not read).
        std::size_t size() const noexcept { return size_; }

        bool empty() const noexcept { return size_ == 0; }

        // The memory as an array of T (uint8_t pixels, float samples). The address of a direct buffer is aligned for
        // any T that Java itself can put into it.
        template<class T>
        T *as() const noexcept {
            return reinterpret_cast<T *>(data_);
        }

    private:
        std::byte *data_ = nullptr;
        std::size_t size_ = 0;
    };

    namespace detail {
        // GetDirectBufferAddress / GetDirectBufferCapacity. A buffer that is not direct is an IllegalArgument.
        DirectBuffer readDirectBuffer(JNIEnv *env, jobject buffer);
    } // namespace detail

    // ---- rows ----

    // IntArray, FloatArray, ... : a copy, in a vector. ByteArray is std::vector<jbyte> or std::vector<std::byte>.
    template<class E>
    struct Convert<std::vector<E>, std::enable_if_t<detail::isNumberElement<E> > > {
    private:
        using Traits = detail::ArrayTraits<typename detail::ArrayOfElement<E>::Array>;
        using Element = typename Traits::Element;

    public:
        using Jni = typename detail::ArrayOfElement<E>::Array;
        using Value = std::vector<E>;
        static constexpr auto descriptor = Traits::descriptor;

        static Value fromJava(JNIEnv *env, Jni array) {
            Value result(static_cast<std::size_t>(env->GetArrayLength(array)));
            if (!result.empty()) {
                Traits::get(env, array, 0, static_cast<jsize>(result.size()),
                            reinterpret_cast<Element *>(result.data()));
                detail::throwIfPending(env);
            }
            return result;
        }

        static LocalRef<Jni> toJava(JNIEnv *env, const Value &value) {
            LocalRef<Jni> array(env, Traits::make(env, detail::arraySize(value.size())));
            if (!array) detail::throwPendingException(env); // OutOfMemoryError
            if (!value.empty()) {
                Traits::set(env, array.get(), 0, static_cast<jsize>(value.size()),
                            reinterpret_cast<const Element *>(value.data()));
                detail::throwIfPending(env);
            }
            return array;
        }
    };

    // BooleanArray. std::vector<bool> keeps bits, so it goes through a temporary array of jboolean.
    template<>
    struct Convert<std::vector<bool> > {
        using Jni = jbooleanArray;
        using Value = std::vector<bool>;
        static constexpr auto descriptor = detail::text("[Z");

        static Value fromJava(JNIEnv *env, jbooleanArray array) {
            std::vector<jboolean> flags(static_cast<std::size_t>(env->GetArrayLength(array)));
            if (!flags.empty()) {
                env->GetBooleanArrayRegion(array, 0, static_cast<jsize>(flags.size()), flags.data());
                detail::throwIfPending(env);
            }
            Value result(flags.size());
            for (std::size_t i = 0; i < flags.size(); ++i) result[i] = flags[i] != JNI_FALSE;
            return result;
        }

        static LocalRef<jbooleanArray> toJava(JNIEnv *env, const Value &value) {
            LocalRef<jbooleanArray> array(env, env->NewBooleanArray(detail::arraySize(value.size())));
            if (!array) detail::throwPendingException(env);
            if (!value.empty()) {
                std::vector<jboolean> flags(value.size());
                for (std::size_t i = 0; i < flags.size(); ++i) flags[i] = value[i] ? JNI_TRUE : JNI_FALSE;
                env->SetBooleanArrayRegion(array.get(), 0, static_cast<jsize>(flags.size()), flags.data());
                detail::throwIfPending(env);
            }
            return array;
        }
    };

    // std::vector<uint8_t> is std::vector<jboolean>, and a ByteArray is what people mean. Better a message than a guess.
    template<class E>
    struct Convert<std::vector<E>, std::enable_if_t<std::is_same_v<E, jboolean> > > {
        static_assert(detail::AlwaysFalse<E>::value,
                      "jnirt: std::vector<uint8_t> is std::vector<jboolean>: uint8_t and jboolean are the same type, "
                      "so it is not clear whether you mean a ByteArray or a BooleanArray. Use std::vector<std::byte> "
                      "(or std::vector<jbyte>) for ByteArray and std::vector<bool> for BooleanArray.");
        static constexpr auto descriptor = detail::text("?");
    };

    // A direct ByteBuffer, from Java only.
    template<>
    struct Convert<DirectBuffer> {
        using Jni = jobject;
        using Value = DirectBuffer;
        static constexpr auto descriptor = detail::text("Ljava/nio/ByteBuffer;");
        // The memory belongs to the Java object: good while the object is alive (a native parameter), not for the
        // result of a call, where the object may be collected as soon as the local reference is gone.
        static constexpr bool borrowsFromJava = true;

        static DirectBuffer fromJava(JNIEnv *env, jobject buffer) { return detail::readDirectBuffer(env, buffer); }

        template<class T = DirectBuffer>
        static LocalRef<jobject> toJava(JNIEnv *, const DirectBuffer &) {
            static_assert(detail::AlwaysFalse<T>::value,
                          "jnirt: a DirectBuffer can only come from Java. A ByteBuffer over memory of native code "
                          "needs an owner for that memory (planned for the peer model).");
            return {};
        }
    };
} // namespace jnirt

#endif //JNIRT_RUNTIME_DETAIL_CONVERT_ARRAY_H
