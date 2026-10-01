// jnirt::init, the whole of JNI_OnLoad. init can be called once per process, so this binary has one success test;
// the problems it reports are covered by registry_test.cpp.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/support/logging/macros.h>

#include "class_cache_internal.h"
#include "test_jvm.h"

#include <string>

LOG_DEFINE_TAG("inittest")

using namespace jnirt;

namespace {
    struct ListenerJ : JavaClass<ListenerJ> {
        static constexpr const char *name = "jnirt/test/Listener";

        static inline Method<ListenerJ, void(jint, jstring)> onFrame{"onFrame"};
    };

    struct CameraJ : JavaClass<CameraJ> {
        static constexpr const char *name = "jnirt/test/Camera";

        static inline Constructor<CameraJ, void()> create;
        static inline Method<CameraJ, jboolean(jstring)> nativeOpen{"nativeOpen"};
        static inline StaticMethod<CameraJ, jint(jint)> nativeStatic{"nativeStatic"};
    };

    jboolean nativeOpen(JNIEnv *, jobject, jstring) { return JNI_TRUE; }

    jint nativeStatic(JNIEnv *, jclass, jint x) { return x + 1; }

    void nativeSubscribeStub(JNIEnv *, jobject, jobject) {
    }

    void doNothing(JNIEnv *, jobject) {
    }

    void doNothingStatic(JNIEnv *, jclass) {
    }

    void registerCamera(Registry &r) {
        r.add<CameraJ>()
                .native<&nativeOpen>("nativeOpen")
                .native<&nativeStatic>("nativeStatic")
                .native<&doNothing>("forgotten")
                .native<&doNothing>("instanceOnly")
                .native<&doNothingStatic>("staticOnly")
                .native<&nativeSubscribeStub, void(ListenerJ)>("nativeSubscribe");
        r.add<ListenerJ>();
    }

    // Raw JNI on purpose: described in a class_cache struct, jnirt.NativeLogBridge would have to be added to the
    // registry, and it is jnirt's own class.
    std::string receivedByKotlin(JNIEnv *env) {
        jclass bridge = env->FindClass("jnirt/NativeLogBridge");
        jmethodID all = env->GetStaticMethodID(bridge, "all", "()Ljava/lang/String;");
        jobject lines = env->CallStaticObjectMethod(bridge, all);
        if (env->ExceptionCheck()) return "<exception>";
        LocalRef<jstring> text(env, static_cast<jstring>(lines));
        env->DeleteLocalRef(bridge);
        return detail::toUtf8(env, text.get());
    }

    TEST(Init, FindsRegistersAndReturnsTheJniVersion) {
        JavaVM *vm = jnirt::vm();
        EXPECT_EQ(jnirt::init(vm, {registerCamera}), JNI_VERSION_1_6);

        JNIEnv *env = jnirt::env();
        auto camera = CameraJ::create(env);
        EXPECT_TRUE(CameraJ::nativeOpen(env, camera.get(), nullptr));
        EXPECT_EQ(CameraJ::nativeStatic(env, 1), 2);
    }

    // init() can run once per process and ctest runs each test in its own process, so these steps follow init in one test.
    TEST(Init, NativeLogLinesGoToKotlinAndKotlinSetsTheLevel) {
        JavaVM *vm = jnirt::vm();
        ASSERT_EQ(jnirt::init(vm, {registerCamera}), JNI_VERSION_1_6);
        JNIEnv *env = jnirt::env();

        EXPECT_NE(receivedByKotlin(env).find("4/jnirt/startup done: 2 classes"), std::string::npos);
        LOGW("after init");
        EXPECT_NE(receivedByKotlin(env).find("5/inittest/after init"), std::string::npos);

        // setNativeLevel is registered by init itself
        jclass bridge = env->FindClass("jnirt/NativeLogBridge");
        jmethodID setLevel = env->GetStaticMethodID(bridge, "setNativeLevel", "(I)V");
        env->CallStaticVoidMethod(bridge, setLevel, 6); // android.util.Log.ERROR
        EXPECT_FALSE(env->ExceptionCheck());
        EXPECT_FALSE(jnirt::logging::enabled(jnirt::logging::Level::Warn));
        env->CallStaticVoidMethod(bridge, setLevel, 3); // android.util.Log.DEBUG
        EXPECT_TRUE(jnirt::logging::enabled(jnirt::logging::Level::Debug));
        env->DeleteLocalRef(bridge);
    }

    TEST(Init, ASecondCallIsACrash) {
        GTEST_FLAG_SET(death_test_style, "threadsafe");
        JavaVM *vm = jnirt::vm();
        EXPECT_DEATH({
                         jnirt::init(vm, {});
                         jnirt::init(vm, {});
                     }, "jnirt::init was called twice");
    }
} // namespace
