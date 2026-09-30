// jnirt::env / jnirt::detachCurrentThread against a real JVM (JNI_CreateJavaVM, -Xcheck:jni).
//
// One JVM per test binary, created before the first test and never destroyed: HotSpot can not create a second
// VM in the same process. The main thread is attached by JNI_CreateJavaVM, so it plays "a Java thread".

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/vm.h>
#include <jnirt/support/error.h>

#include <gtest/gtest.h>

#include <pthread.h>

#include <string>
#include <thread>

namespace {
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

    // What GetEnv says about the current thread, without jnirt.
    jint attachState() {
        JNIEnv *env = nullptr;
        return gJvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    }

    jobject currentJavaThread(JNIEnv *env) {
        jclass threadClass = env->FindClass("java/lang/Thread");
        jmethodID currentThread = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
        jobject thread = env->CallStaticObjectMethod(threadClass, currentThread);
        EXPECT_FALSE(env->ExceptionCheck());
        jobject global = env->NewGlobalRef(thread);
        env->DeleteLocalRef(thread);
        env->DeleteLocalRef(threadClass);
        return global;
    }

    bool isAlive(JNIEnv *env, jobject thread) {
        jclass threadClass = env->GetObjectClass(thread);
        jmethodID isAliveMethod = env->GetMethodID(threadClass, "isAlive", "()Z");
        bool alive = env->CallBooleanMethod(thread, isAliveMethod) == JNI_TRUE;
        EXPECT_FALSE(env->ExceptionCheck());
        env->DeleteLocalRef(threadClass);
        return alive;
    }

    std::string javaName(JNIEnv *env, jobject thread) {
        jclass threadClass = env->GetObjectClass(thread);
        jmethodID getName = env->GetMethodID(threadClass, "getName", "()Ljava/lang/String;");
        auto name = static_cast<jstring>(env->CallObjectMethod(thread, getName));
        EXPECT_FALSE(env->ExceptionCheck());
        const char *chars = env->GetStringUTFChars(name, nullptr);
        std::string result(chars);
        env->ReleaseStringUTFChars(name, chars);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(threadClass);
        return result;
    }

    // Runs `body` on a fresh native thread with the given pthread name and waits for it to exit
    // (pthread_join returns after the thread's key destructors ran).
    template<class Body>
    void onNativeThread(const char *name, Body body) {
        std::thread thread([&] {
            pthread_setname_np(pthread_self(), name);
            body();
        });
        thread.join();
    }
} // namespace

// ---- Java thread (attached by the JVM) ---------------------------------------------------------------------

TEST(Env, JavaThreadGetsItsOwnEnv) {
    JNIEnv *expected = nullptr;
    ASSERT_EQ(gJvm->GetEnv(reinterpret_cast<void **>(&expected), JNI_VERSION_1_6), JNI_OK);
    EXPECT_EQ(jnirt::env(), expected);
}

TEST(Env, DetachingJavaThreadThrowsIllegalStateAndKeepsItAttached) {
    try {
        jnirt::detachCurrentThread();
        FAIL() << "did not throw";
    } catch (const jnirt::IllegalState &e) {
        EXPECT_NE(std::string(e.what()).find("not by jnirt"), std::string::npos) << e.what();
    }
    EXPECT_EQ(attachState(), JNI_OK);
}

// ---- Native thread attached by someone else ----------------------------------------------------------------

TEST(Env, ForeignAttachIsLeftAlone) {
    onNativeThread("foreign", [] {
        JNIEnv *foreignEnv = nullptr;
        ASSERT_EQ(gJvm->AttachCurrentThread(reinterpret_cast<void **>(&foreignEnv), nullptr), JNI_OK);

        EXPECT_EQ(jnirt::env(), foreignEnv);
        EXPECT_THROW(jnirt::detachCurrentThread(), jnirt::IllegalState);
        EXPECT_EQ(attachState(), JNI_OK);

        EXPECT_EQ(gJvm->DetachCurrentThread(), JNI_OK); // the owner detaches; jnirt did not do it
    });
}

// ---- Native thread attached by jnirt -----------------------------------------------------------------------

TEST(Env, NativeThreadIsAttachedUnderItsNameAndDetachedAtExit) {
    jobject javaThread = nullptr;
    onNativeThread("jnirt-camera", [&] {
        ASSERT_EQ(attachState(), JNI_EDETACHED);
        JNIEnv *env = jnirt::env();
        ASSERT_NE(env, nullptr);
        EXPECT_EQ(attachState(), JNI_OK);
        javaThread = currentJavaThread(env);
        EXPECT_EQ(javaName(env, javaThread), "jnirt-camera");
    });

    JNIEnv *env = jnirt::env();
    EXPECT_FALSE(isAlive(env, javaThread)) << "the thread was not detached at exit";
    env->DeleteGlobalRef(javaThread);
}

TEST(Env, RepeatedCallsReturnTheSameEnv) {
    onNativeThread("jnirt-repeat", [] {
        JNIEnv *first = jnirt::env();
        EXPECT_EQ(jnirt::env(), first);
        EXPECT_EQ(jnirt::env(), first);
    });
}

TEST(Env, ManualDetachDetachesNowAndEnvAttachesAgain) {
    jobject firstJavaThread = nullptr;
    jobject secondJavaThread = nullptr;
    onNativeThread("jnirt-pool", [&] {
        firstJavaThread = currentJavaThread(jnirt::env());
        jnirt::detachCurrentThread();
        EXPECT_EQ(attachState(), JNI_EDETACHED);

        secondJavaThread = currentJavaThread(jnirt::env()); // attached again, detached at exit
        EXPECT_EQ(attachState(), JNI_OK);
    });

    JNIEnv *env = jnirt::env();
    EXPECT_FALSE(isAlive(env, firstJavaThread));
    EXPECT_FALSE(isAlive(env, secondJavaThread));
    env->DeleteGlobalRef(firstJavaThread);
    env->DeleteGlobalRef(secondJavaThread);
}

TEST(Env, ManualDetachOnNotAttachedThreadDoesNothing) {
    onNativeThread("jnirt-idle", [] {
        EXPECT_NO_THROW(jnirt::detachCurrentThread());
        EXPECT_EQ(attachState(), JNI_EDETACHED);
    });
}

TEST(Env, ManyThreadsAttachAndDetach) {
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([] {
            for (int round = 0; round < 20; ++round) {
                ASSERT_NE(jnirt::env(), nullptr);
                if (round % 2 == 0) jnirt::detachCurrentThread();
            }
        });
    }
    for (auto &thread: threads) thread.join();
    EXPECT_EQ(attachState(), JNI_OK); // the main (Java) thread is untouched
}
