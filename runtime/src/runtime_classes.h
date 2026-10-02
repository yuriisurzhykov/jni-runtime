// The Java classes jnirt itself talks to, described like any user class (class_cache.h): the same lookup, the same
// error messages, the same debug checks. init() finds them before the user's classes (runtimeClasses() in init.cpp).
// Defined in a header, so every file that uses one sees the same ClassInfo.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_RUNTIME_CLASSES_H
#define JNIRT_RUNTIME_RUNTIME_CLASSES_H

#pragma once
#include <jnirt/runtime/class_cache.h>

namespace jnirt::detail {
    // The two members that put a Java exception into words.
    struct ThrowableJ : JavaClass<ThrowableJ> {
        static constexpr const char *name = "java/lang/Throwable";

        static inline Method<ThrowableJ, jstring()> toString{"toString"};
        static inline Method<ThrowableJ, ThrowableJ()> getCause{"getCause"};
    };

    // What entry() throws for a jnirt::Error (android/.../NativeException.kt).
    struct NativeExceptionJ : JavaClass<NativeExceptionJ> {
        static constexpr const char *name = "jnirt/NativeException";

        // (domain, code, message, cause)
        static inline Constructor<NativeExceptionJ, void(jstring, jint, jstring, ThrowableJ)> create;
    };

    struct IllegalArgumentExceptionJ : JavaClass<IllegalArgumentExceptionJ> {
        static constexpr const char *name = "java/lang/IllegalArgumentException";

        static inline Constructor<IllegalArgumentExceptionJ, void(jstring, ThrowableJ)> create;
    };

    struct IllegalStateExceptionJ : JavaClass<IllegalStateExceptionJ> {
        static constexpr const char *name = "java/lang/IllegalStateException";

        static inline Constructor<IllegalStateExceptionJ, void(jstring, ThrowableJ)> create;
    };

    struct OutOfMemoryErrorJ : JavaClass<OutOfMemoryErrorJ> {
        static constexpr const char *name = "java/lang/OutOfMemoryError";

        static inline Constructor<OutOfMemoryErrorJ, void(jstring)> create;
    };

    // System.gc(): asked for when the native memory of ByteBuffers piles up (native_buffer.cpp).
    struct SystemJ : JavaClass<SystemJ> {
        static constexpr const char *name = "java/lang/System";

        static inline StaticMethod<SystemJ, void()> gc{"gc"};
    };

    // The boxes of the numbers: std::optional<jint> is an Integer (convert.cpp).
    struct BooleanJ : JavaClass<BooleanJ> {
        static constexpr const char *name = "java/lang/Boolean";

        static inline StaticMethod<BooleanJ, BooleanJ(jboolean)> valueOf{"valueOf"};
        static inline Method<BooleanJ, jboolean()> booleanValue{"booleanValue"};
    };

    struct ByteJ : JavaClass<ByteJ> {
        static constexpr const char *name = "java/lang/Byte";

        static inline StaticMethod<ByteJ, ByteJ(jbyte)> valueOf{"valueOf"};
        static inline Method<ByteJ, jbyte()> byteValue{"byteValue"};
    };

    struct CharacterJ : JavaClass<CharacterJ> {
        static constexpr const char *name = "java/lang/Character";

        static inline StaticMethod<CharacterJ, CharacterJ(jchar)> valueOf{"valueOf"};
        static inline Method<CharacterJ, jchar()> charValue{"charValue"};
    };

    struct ShortJ : JavaClass<ShortJ> {
        static constexpr const char *name = "java/lang/Short";

        static inline StaticMethod<ShortJ, ShortJ(jshort)> valueOf{"valueOf"};
        static inline Method<ShortJ, jshort()> shortValue{"shortValue"};
    };

    struct IntegerJ : JavaClass<IntegerJ> {
        static constexpr const char *name = "java/lang/Integer";

        static inline StaticMethod<IntegerJ, IntegerJ(jint)> valueOf{"valueOf"};
        static inline Method<IntegerJ, jint()> intValue{"intValue"};
    };

    struct LongJ : JavaClass<LongJ> {
        static constexpr const char *name = "java/lang/Long";

        static inline StaticMethod<LongJ, LongJ(jlong)> valueOf{"valueOf"};
        static inline Method<LongJ, jlong()> longValue{"longValue"};
    };

    struct FloatJ : JavaClass<FloatJ> {
        static constexpr const char *name = "java/lang/Float";

        static inline StaticMethod<FloatJ, FloatJ(jfloat)> valueOf{"valueOf"};
        static inline Method<FloatJ, jfloat()> floatValue{"floatValue"};
    };

    struct DoubleJ : JavaClass<DoubleJ> {
        static constexpr const char *name = "java/lang/Double";

        static inline StaticMethod<DoubleJ, DoubleJ(jdouble)> valueOf{"valueOf"};
        static inline Method<DoubleJ, jdouble()> doubleValue{"doubleValue"};
    };

    // The collections (convert.cpp): a List is read with toArray() and written as an ArrayList, a Map is read through
    // entrySet().toArray() and written as a LinkedHashMap.
    struct CollectionJ : JavaClass<CollectionJ> {
        static constexpr const char *name = "java/util/Collection";

        static inline Method<CollectionJ, Array<Object>()> toArray{"toArray"};
    };

    struct SetJ : JavaClass<SetJ> {
        static constexpr const char *name = "java/util/Set";
    };

    struct ArrayListJ : JavaClass<ArrayListJ> {
        static constexpr const char *name = "java/util/ArrayList";

        static inline Constructor<ArrayListJ, void(jint)> create;
        static inline Method<ArrayListJ, jboolean(Object)> add{"add"};
    };

    struct MapEntryJ : JavaClass<MapEntryJ> {
        static constexpr const char *name = "java/util/Map$Entry";

        static inline Method<MapEntryJ, Object()> getKey{"getKey"};
        static inline Method<MapEntryJ, Object()> getValue{"getValue"};
    };

    struct MapJ : JavaClass<MapJ> {
        static constexpr const char *name = "java/util/Map";

        static inline Method<MapJ, SetJ()> entrySet{"entrySet"};
        static inline Method<MapJ, Object(Object, Object)> put{"put"};
    };

    struct LinkedHashMapJ : JavaClass<LinkedHashMapJ> {
        static constexpr const char *name = "java/util/LinkedHashMap";

        static inline Constructor<LinkedHashMapJ, void(jint)> create;
    };

    // Where native log lines go (android/.../NativeLogBridge.kt). `setNativeLevel` is native: init registers it.
    struct NativeLogBridgeJ : JavaClass<NativeLogBridgeJ> {
        static constexpr const char *name = "jnirt/NativeLogBridge";

        // (priority as in android.util.Log, tag, message)
        static inline StaticMethod<NativeLogBridgeJ, void(jint, jstring, jstring)> log{"log"};
    };

    // Ties native memory to a ByteBuffer (android/.../NativeBuffers.kt): `watch` closes the handle when the buffer is
    // collected.
    struct NativeBuffersJ : JavaClass<NativeBuffersJ> {
        static constexpr const char *name = "jnirt/NativeBuffers";

        static inline StaticMethod<NativeBuffersJ, void(Object, jlong)> watch{"watch"};
    };

    // Where NativePeer.close() goes (android/.../NativePeer.kt). `close` is native: init registers it.
    struct NativePeerNativesJ : JavaClass<NativePeerNativesJ> {
        static constexpr const char *name = "jnirt/NativePeerNatives";
    };
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_RUNTIME_CLASSES_H
