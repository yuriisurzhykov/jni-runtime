// jnirt::descriptor / jnirt::signature (ADR-0005).
//
// Most of this file is static_assert: if it compiles, the table and the gluing are right, no JVM needed. The tests at
// the bottom check the table against a real JVM (JNI_CreateJavaVM, -Xcheck:jni): lookups of JDK members with
// generated signatures must succeed.
//
// What must NOT compile (jobject, unsigned types, long long on 64-bit, "com.acme.Camera" as a name) is checked by
// hand; the compiler output is in docs/learning/signatures-by-example.md.

#include <jnirt/runtime/signature.h>

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/ref.h>
#include <jnirt/runtime/vm.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

// Stand-in until class_cache.h (P3.5) defines JavaClass: signature.h only needs it to exist.
namespace jnirt {
    template<class Self>
    struct JavaClass {};
} // namespace jnirt

namespace {
    using jnirt::descriptor;
    using jnirt::signature;
    using std::string_view;

    struct FrameJ : jnirt::JavaClass<FrameJ> {
        static constexpr const char *name = "com/acme/camera/Frame";
    };

    struct StateJ : jnirt::JavaClass<StateJ> {
        static constexpr const char *name = "com/acme/camera/Camera$State";
    };

    // A member whose signature names the class it belongs to: the signature is built only when asked for.
    template<class F>
    struct MemberStub {
        const char *sig() const { return signature<F>; }
    };

    struct CameraJ : jnirt::JavaClass<CameraJ> {
        static constexpr const char *name = "com/acme/camera/Camera";
        static inline MemberStub<CameraJ(FrameJ)> withFrame{};
    };

    // Primitives and void
    static_assert(string_view(descriptor<void>) == "V");
    static_assert(string_view(descriptor<jboolean>) == "Z");
    static_assert(string_view(descriptor<jbyte>) == "B");
    static_assert(string_view(descriptor<jchar>) == "C");
    static_assert(string_view(descriptor<jshort>) == "S");
    static_assert(string_view(descriptor<jint>) == "I");
    static_assert(string_view(descriptor<jlong>) == "J");
    static_assert(string_view(descriptor<jfloat>) == "F");
    static_assert(string_view(descriptor<jdouble>) == "D");

    // Reference types with one meaning
    static_assert(string_view(descriptor<jstring>) == "Ljava/lang/String;");
    static_assert(string_view(descriptor<jclass>) == "Ljava/lang/Class;");
    static_assert(string_view(descriptor<jthrowable>) == "Ljava/lang/Throwable;");

    // Primitive arrays
    static_assert(string_view(descriptor<jbooleanArray>) == "[Z");
    static_assert(string_view(descriptor<jbyteArray>) == "[B");
    static_assert(string_view(descriptor<jcharArray>) == "[C");
    static_assert(string_view(descriptor<jshortArray>) == "[S");
    static_assert(string_view(descriptor<jintArray>) == "[I");
    static_assert(string_view(descriptor<jlongArray>) == "[J");
    static_assert(string_view(descriptor<jfloatArray>) == "[F");
    static_assert(string_view(descriptor<jdoubleArray>) == "[D");

    // Described classes
    static_assert(string_view(descriptor<FrameJ>) == "Lcom/acme/camera/Frame;");
    static_assert(string_view(descriptor<StateJ>) == "Lcom/acme/camera/Camera$State;");

    // Aliases that are the same C++ types as JNI ones
    static_assert(string_view(descriptor<int>) == "I");
    static_assert(string_view(descriptor<jsize>) == "I");
    static_assert(string_view(descriptor<std::int64_t>) == "J");

    // Signatures
    static_assert(string_view(signature<void()>) == "()V");
    static_assert(string_view(signature<jint()>) == "()I");
    static_assert(string_view(signature<void(jint, jstring)>) == "(ILjava/lang/String;)V");
    static_assert(string_view(signature<jboolean(jlong, jintArray)>) == "(J[I)Z");
    static_assert(string_view(signature<jstring(jbyteArray, jint, jint)>) == "([BII)Ljava/lang/String;");
    static_assert(string_view(signature<void(FrameJ, jint)>) == "(Lcom/acme/camera/Frame;I)V");
    static_assert(string_view(signature<CameraJ(FrameJ, StateJ)>) ==
                  "(Lcom/acme/camera/Frame;Lcom/acme/camera/Camera$State;)Lcom/acme/camera/Camera;");

    TEST(Signature, ValuesAreNullTerminatedStrings) {
        EXPECT_STREQ(signature<void(jint, jstring)>, "(ILjava/lang/String;)V");
        EXPECT_STREQ(descriptor<FrameJ>, "Lcom/acme/camera/Frame;");
    }

    TEST(Signature, ClassCanNameItselfInItsOwnMembers) {
        EXPECT_STREQ(CameraJ::withFrame.sig(), "(Lcom/acme/camera/Frame;)Lcom/acme/camera/Camera;");
    }

    // ---- The table against a real JVM ----

    JavaVM *gJvm = nullptr;

    class JvmEnvironment : public ::testing::Environment {
    public:
        void SetUp() override {
            JavaVMOption options[] = {{const_cast<char *>("-Xcheck:jni"), nullptr}};
            JavaVMInitArgs args{JNI_VERSION_1_6, 1, options, JNI_FALSE};
            JNIEnv *env = nullptr;
            ASSERT_EQ(JNI_CreateJavaVM(&gJvm, reinterpret_cast<void **>(&env), &args), JNI_OK);
            jnirt::setVm(gJvm);
        }
    };

    const auto *gJvmEnvironment = ::testing::AddGlobalTestEnvironment(new JvmEnvironment);

    struct StringJ : jnirt::JavaClass<StringJ> {
        static constexpr const char *name = "java/lang/String";
    };

    struct LocaleJ : jnirt::JavaClass<LocaleJ> {
        static constexpr const char *name = "java/util/Locale";
    };

    struct ComparatorJ : jnirt::JavaClass<ComparatorJ> {
        static constexpr const char *name = "java/util/Comparator";
    };

    struct ThreadStateJ : jnirt::JavaClass<ThreadStateJ> {
        static constexpr const char *name = "java/lang/Thread$State";
    };

    class SignatureOnJvm : public ::testing::Test {
    protected:
        void SetUp() override { env = jnirt::env(); }

        void TearDown() override { EXPECT_FALSE(env->ExceptionCheck()); }

        jnirt::LocalRef<jclass> findClass(const char *name) const {
            jnirt::LocalRef<jclass> cls(env, env->FindClass(name));
            EXPECT_TRUE(cls) << name;
            return cls;
        }

        // Clears the NoSuchMethodError / NoSuchFieldError of a failed lookup, so the failure is reported by the
        // EXPECT and not by -Xcheck:jni.
        template<class Id>
        bool found(Id id) const {
            if (id != nullptr) return true;
            env->ExceptionClear();
            return false;
        }

        JNIEnv *env = nullptr;
    };

    TEST_F(SignatureOnJvm, InstanceMethods) {
        auto string = findClass("java/lang/String");
        EXPECT_TRUE(found(env->GetMethodID(string.get(), "length", signature<jint()>)));
        EXPECT_TRUE(found(env->GetMethodID(string.get(), "charAt", signature<jchar(jint)>)));
        EXPECT_TRUE(found(env->GetMethodID(string.get(), "isEmpty", signature<jboolean()>)));
        EXPECT_TRUE(found(env->GetMethodID(string.get(), "getBytes", signature<jbyteArray()>)));
        EXPECT_TRUE(found(env->GetMethodID(string.get(), "toCharArray", signature<jcharArray()>)));
        EXPECT_TRUE(found(env->GetMethodID(string.get(), "substring", signature<jstring(jint, jint)>)));
        EXPECT_TRUE(found(env->GetMethodID(string.get(), "toUpperCase", signature<jstring(LocaleJ)>)));
        EXPECT_TRUE(found(env->GetMethodID(string.get(), "<init>", signature<void(jcharArray, jint, jint)>)));
    }

    TEST_F(SignatureOnJvm, StaticMethods) {
        auto string = findClass("java/lang/String");
        EXPECT_TRUE(found(env->GetStaticMethodID(string.get(), "valueOf", signature<jstring(jint)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(string.get(), "valueOf", signature<jstring(jlong)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(string.get(), "valueOf", signature<jstring(jdouble)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(string.get(), "valueOf", signature<jstring(jfloat)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(string.get(), "valueOf", signature<jstring(jboolean)>)));

        auto threadState = findClass("java/lang/Thread$State");
        EXPECT_TRUE(found(env->GetStaticMethodID(threadState.get(), "valueOf", signature<ThreadStateJ(jstring)>)));

        auto arrays = findClass("java/util/Arrays");
        EXPECT_TRUE(found(env->GetStaticMethodID(arrays.get(), "hashCode", signature<jint(jshortArray)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(arrays.get(), "hashCode", signature<jint(jfloatArray)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(arrays.get(), "hashCode", signature<jint(jdoubleArray)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(arrays.get(), "hashCode", signature<jint(jbooleanArray)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(arrays.get(), "hashCode", signature<jint(jintArray)>)));
        EXPECT_TRUE(found(env->GetStaticMethodID(arrays.get(), "hashCode", signature<jint(jlongArray)>)));

        auto byteClass = findClass("java/lang/Byte");
        EXPECT_TRUE(found(env->GetStaticMethodID(byteClass.get(), "toString", signature<jstring(jbyte)>)));
        auto shortClass = findClass("java/lang/Short");
        EXPECT_TRUE(found(env->GetStaticMethodID(shortClass.get(), "toString", signature<jstring(jshort)>)));
    }

    TEST_F(SignatureOnJvm, ClassAndThrowable) {
        auto object = findClass("java/lang/Object");
        EXPECT_TRUE(found(env->GetMethodID(object.get(), "getClass", signature<jclass()>)));
        auto throwable = findClass("java/lang/Throwable");
        EXPECT_TRUE(found(env->GetMethodID(throwable.get(), "getCause", signature<jthrowable()>)));
    }

    TEST_F(SignatureOnJvm, Fields) {
        auto integer = findClass("java/lang/Integer");
        EXPECT_TRUE(found(env->GetStaticFieldID(integer.get(), "MAX_VALUE", descriptor<jint>)));
        auto longClass = findClass("java/lang/Long");
        EXPECT_TRUE(found(env->GetStaticFieldID(longClass.get(), "MIN_VALUE", descriptor<jlong>)));
        auto string = findClass("java/lang/String");
        EXPECT_TRUE(found(env->GetStaticFieldID(string.get(), "CASE_INSENSITIVE_ORDER", descriptor<ComparatorJ>)));
    }

    // A wrong type is a failed lookup at startup, not a crash later: the case the compiler can not see (ADR-0005 §7).
    TEST_F(SignatureOnJvm, WrongTypeFailsTheLookup) {
        auto string = findClass("java/lang/String");
        EXPECT_FALSE(found(env->GetMethodID(string.get(), "length", signature<jlong()>)));
        EXPECT_FALSE(found(env->GetMethodID(string.get(), "toUpperCase", signature<jstring(StringJ)>)));
    }
} // namespace
