// jnirt::init, the whole of JNI_OnLoad. init can be called once per process, so this binary has one success test;
// the problems it reports are covered by registry_test.cpp.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>

#include "test_jvm.h"

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

    TEST(Init, FindsRegistersAndReturnsTheJniVersion) {
        JavaVM *vm = jnirt::vm();
        EXPECT_EQ(jnirt::init(vm, {registerCamera}), JNI_VERSION_1_6);

        JNIEnv *env = jnirt::env();
        auto camera = CameraJ::create(env);
        EXPECT_TRUE(CameraJ::nativeOpen(env, camera.get(), nullptr));
        EXPECT_EQ(CameraJ::nativeStatic(env, 1), 2);
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
