// Not for direct use. The JNI call that fits a C++ type: Call<Type>Method, Get<Type>Field and so on, chosen at
// compile time, so Method<C, jint(jint)> ends up in CallIntMethod. Used by class_cache.h.
//
// R is void, a primitive (jboolean ... jdouble) or jobject: every reference type goes through the jobject call,
// and class_cache casts the result back.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_DETAIL_JNI_CALLS_H
#define JNIRT_RUNTIME_DETAIL_JNI_CALLS_H

#pragma once
#include <jni.h>

#include <type_traits>

namespace jnirt::detail {
    template<class T>
    constexpr bool isObjectCallType = std::is_same_v<T, jobject>;

    // Arguments go through the variadic JNI calls: the JNI spec fixes the promotions (jboolean, jbyte, jshort,
    // jchar to int, jfloat to double), and the compiler applies the same ones to `args`.
    template<class R, class... A>
    R callMethod(JNIEnv *env, jobject self, jmethodID id, A... args) {
        if constexpr (std::is_void_v<R>) env->CallVoidMethod(self, id, args...);
        else if constexpr (std::is_same_v<R, jboolean>) return env->CallBooleanMethod(self, id, args...);
        else if constexpr (std::is_same_v<R, jbyte>) return env->CallByteMethod(self, id, args...);
        else if constexpr (std::is_same_v<R, jchar>) return env->CallCharMethod(self, id, args...);
        else if constexpr (std::is_same_v<R, jshort>) return env->CallShortMethod(self, id, args...);
        else if constexpr (std::is_same_v<R, jint>) return env->CallIntMethod(self, id, args...);
        else if constexpr (std::is_same_v<R, jlong>) return env->CallLongMethod(self, id, args...);
        else if constexpr (std::is_same_v<R, jfloat>) return env->CallFloatMethod(self, id, args...);
        else if constexpr (std::is_same_v<R, jdouble>) return env->CallDoubleMethod(self, id, args...);
        else {
            static_assert(isObjectCallType<R>, "jnirt: internal error, unexpected call result type");
            return env->CallObjectMethod(self, id, args...);
        }
    }

    template<class R, class... A>
    R callStaticMethod(JNIEnv *env, jclass cls, jmethodID id, A... args) {
        if constexpr (std::is_void_v<R>) env->CallStaticVoidMethod(cls, id, args...);
        else if constexpr (std::is_same_v<R, jboolean>) return env->CallStaticBooleanMethod(cls, id, args...);
        else if constexpr (std::is_same_v<R, jbyte>) return env->CallStaticByteMethod(cls, id, args...);
        else if constexpr (std::is_same_v<R, jchar>) return env->CallStaticCharMethod(cls, id, args...);
        else if constexpr (std::is_same_v<R, jshort>) return env->CallStaticShortMethod(cls, id, args...);
        else if constexpr (std::is_same_v<R, jint>) return env->CallStaticIntMethod(cls, id, args...);
        else if constexpr (std::is_same_v<R, jlong>) return env->CallStaticLongMethod(cls, id, args...);
        else if constexpr (std::is_same_v<R, jfloat>) return env->CallStaticFloatMethod(cls, id, args...);
        else if constexpr (std::is_same_v<R, jdouble>) return env->CallStaticDoubleMethod(cls, id, args...);
        else {
            static_assert(isObjectCallType<R>, "jnirt: internal error, unexpected call result type");
            return env->CallStaticObjectMethod(cls, id, args...);
        }
    }

    template<class T>
    T getField(JNIEnv *env, jobject self, jfieldID id) {
        if constexpr (std::is_same_v<T, jboolean>) return env->GetBooleanField(self, id);
        else if constexpr (std::is_same_v<T, jbyte>) return env->GetByteField(self, id);
        else if constexpr (std::is_same_v<T, jchar>) return env->GetCharField(self, id);
        else if constexpr (std::is_same_v<T, jshort>) return env->GetShortField(self, id);
        else if constexpr (std::is_same_v<T, jint>) return env->GetIntField(self, id);
        else if constexpr (std::is_same_v<T, jlong>) return env->GetLongField(self, id);
        else if constexpr (std::is_same_v<T, jfloat>) return env->GetFloatField(self, id);
        else if constexpr (std::is_same_v<T, jdouble>) return env->GetDoubleField(self, id);
        else {
            static_assert(isObjectCallType<T>, "jnirt: internal error, unexpected field type");
            return env->GetObjectField(self, id);
        }
    }

    template<class T>
    void setField(JNIEnv *env, jobject self, jfieldID id, T value) {
        if constexpr (std::is_same_v<T, jboolean>) env->SetBooleanField(self, id, value);
        else if constexpr (std::is_same_v<T, jbyte>) env->SetByteField(self, id, value);
        else if constexpr (std::is_same_v<T, jchar>) env->SetCharField(self, id, value);
        else if constexpr (std::is_same_v<T, jshort>) env->SetShortField(self, id, value);
        else if constexpr (std::is_same_v<T, jint>) env->SetIntField(self, id, value);
        else if constexpr (std::is_same_v<T, jlong>) env->SetLongField(self, id, value);
        else if constexpr (std::is_same_v<T, jfloat>) env->SetFloatField(self, id, value);
        else if constexpr (std::is_same_v<T, jdouble>) env->SetDoubleField(self, id, value);
        else {
            static_assert(isObjectCallType<T>, "jnirt: internal error, unexpected field type");
            env->SetObjectField(self, id, value);
        }
    }

    template<class T>
    T getStaticField(JNIEnv *env, jclass cls, jfieldID id) {
        if constexpr (std::is_same_v<T, jboolean>) return env->GetStaticBooleanField(cls, id);
        else if constexpr (std::is_same_v<T, jbyte>) return env->GetStaticByteField(cls, id);
        else if constexpr (std::is_same_v<T, jchar>) return env->GetStaticCharField(cls, id);
        else if constexpr (std::is_same_v<T, jshort>) return env->GetStaticShortField(cls, id);
        else if constexpr (std::is_same_v<T, jint>) return env->GetStaticIntField(cls, id);
        else if constexpr (std::is_same_v<T, jlong>) return env->GetStaticLongField(cls, id);
        else if constexpr (std::is_same_v<T, jfloat>) return env->GetStaticFloatField(cls, id);
        else if constexpr (std::is_same_v<T, jdouble>) return env->GetStaticDoubleField(cls, id);
        else {
            static_assert(isObjectCallType<T>, "jnirt: internal error, unexpected field type");
            return env->GetStaticObjectField(cls, id);
        }
    }

    template<class T>
    void setStaticField(JNIEnv *env, jclass cls, jfieldID id, T value) {
        if constexpr (std::is_same_v<T, jboolean>) env->SetStaticBooleanField(cls, id, value);
        else if constexpr (std::is_same_v<T, jbyte>) env->SetStaticByteField(cls, id, value);
        else if constexpr (std::is_same_v<T, jchar>) env->SetStaticCharField(cls, id, value);
        else if constexpr (std::is_same_v<T, jshort>) env->SetStaticShortField(cls, id, value);
        else if constexpr (std::is_same_v<T, jint>) env->SetStaticIntField(cls, id, value);
        else if constexpr (std::is_same_v<T, jlong>) env->SetStaticLongField(cls, id, value);
        else if constexpr (std::is_same_v<T, jfloat>) env->SetStaticFloatField(cls, id, value);
        else if constexpr (std::is_same_v<T, jdouble>) env->SetStaticDoubleField(cls, id, value);
        else {
            static_assert(isObjectCallType<T>, "jnirt: internal error, unexpected field type");
            env->SetStaticObjectField(cls, id, value);
        }
    }
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_DETAIL_JNI_CALLS_H
