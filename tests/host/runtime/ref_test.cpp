// jnirt::LocalRef / GlobalRef / WeakGlobalRef against a real JVM (JNI_CreateJavaVM, -Xcheck:jni).
//
// One JVM per test binary, created before the first test and never destroyed (HotSpot can not create a second VM in
// one process). Every test checks that it leaves the live reference counts where it found them. The counts and the
// misuse checks exist in debug builds only, so in a release build (NDEBUG) the tests are skipped.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/ref.h>
#include <jnirt/runtime/vm.h>

#include "ref_internal.h"

#include <gtest/gtest.h>

#include <pthread.h>

#include <thread>
#include <type_traits>

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

    // Every test starts and ends with no live references made through jnirt.
    class Ref : public ::testing::Test {
    protected:
        static void SetUpTestSuite() { GTEST_FLAG_SET(death_test_style, "threadsafe"); }

        void SetUp() override {
#ifdef NDEBUG
            GTEST_SKIP() << "reference bookkeeping exists only in debug builds";
#endif
            env = jnirt::env();
            ASSERT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 0);
            globalsAtStart = jnirt::detail::liveGlobalRefs();
            weaksAtStart = jnirt::detail::liveWeakGlobalRefs();
        }

        void TearDown() override {
            if (IsSkipped()) return;
            EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 0) << "a LocalRef leaked out of the test";
            EXPECT_EQ(jnirt::detail::liveGlobalRefs(), globalsAtStart) << "a GlobalRef leaked";
            EXPECT_EQ(jnirt::detail::liveWeakGlobalRefs(), weaksAtStart) << "a WeakGlobalRef leaked";
            EXPECT_FALSE(env->ExceptionCheck());
        }

        jnirt::LocalRef<jstring> newString(const char *text) const {
            return jnirt::LocalRef<jstring>(env, env->NewStringUTF(text));
        }

        jnirt::LocalRef<jobject> newObject() const {
            jnirt::LocalRef<jclass> objectClass(env, env->FindClass("java/lang/Object"));
            jmethodID init = env->GetMethodID(objectClass.get(), "<init>", "()V");
            return jnirt::LocalRef<jobject>(env, env->NewObject(objectClass.get(), init));
        }

        // System.gc() until `collected()` holds or the attempts run out: HotSpot does not promise a collection.
        template<class Predicate>
        bool collectUntil(Predicate collected) const {
            jnirt::LocalRef<jclass> systemClass(env, env->FindClass("java/lang/System"));
            jmethodID gc = env->GetStaticMethodID(systemClass.get(), "gc", "()V");
            for (int attempt = 0; attempt < 50; ++attempt) {
                env->CallStaticVoidMethod(systemClass.get(), gc);
                if (env->ExceptionCheck()) return false;
                if (collected()) return true;
            }
            return false;
        }

        JNIEnv *env = nullptr;
        std::size_t globalsAtStart = 0;
        std::size_t weaksAtStart = 0;
    };

    template<class Body>
    void onNativeThread(const char *name, Body body) {
        std::thread thread([&] {
            pthread_setname_np(pthread_self(), name);
            body();
        });
        thread.join();
    }
} // namespace

// ---- Types ---------------------------------------------------------------------------------------------------

static_assert(!std::is_copy_constructible_v<jnirt::LocalRef<>> && !std::is_copy_assignable_v<jnirt::LocalRef<>>);
static_assert(!std::is_copy_constructible_v<jnirt::GlobalRef<>> && !std::is_copy_assignable_v<jnirt::GlobalRef<>>);
static_assert(!std::is_copy_constructible_v<jnirt::WeakGlobalRef<>>);
static_assert(std::is_nothrow_move_constructible_v<jnirt::LocalRef<>>);
static_assert(std::is_nothrow_move_assignable_v<jnirt::GlobalRef<>>);
static_assert(std::is_nothrow_move_constructible_v<jnirt::WeakGlobalRef<>>);
static_assert(std::is_same_v<decltype(std::declval<jnirt::LocalRef<jclass>>().get()), jclass>);
static_assert(!std::is_convertible_v<jnirt::LocalRef<>, bool>, "operator bool must be explicit");

// ---- LocalRef ------------------------------------------------------------------------------------------------

TEST_F(Ref, LocalRefIsDeletedAtTheEndOfItsScope) {
    {
        auto text = newString("hello");
        ASSERT_TRUE(text);
        EXPECT_EQ(env->GetObjectRefType(text.get()), JNILocalRefType);
        EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 1);
    }
    EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 0);
}

TEST_F(Ref, LocalRefInALoopDoesNotPileUp) {
    for (int i = 0; i < 10000; ++i) {
        auto text = newString("frame");
        ASSERT_TRUE(text);
        ASSERT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 1);
    }
}

TEST_F(Ref, EmptyLocalRef) {
    jnirt::LocalRef<jobject> empty;
    EXPECT_FALSE(empty);
    EXPECT_EQ(empty.get(), nullptr);

    jnirt::LocalRef<jobject> fromNull(env, nullptr); // e.g. a Java method returned null
    EXPECT_FALSE(fromNull);
    EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 0);
}

TEST_F(Ref, MovedLocalRefLeavesTheSourceEmpty) {
    auto first = newString("moved");
    jstring raw = first.get();

    jnirt::LocalRef<jstring> second = std::move(first);
    EXPECT_FALSE(first);
    EXPECT_EQ(second.get(), raw);
    EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 1);

    jnirt::LocalRef<jstring> third;
    third = std::move(second);
    EXPECT_FALSE(second);
    EXPECT_EQ(third.get(), raw);
    EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 1);
}

TEST_F(Ref, MoveAssignmentDeletesTheOldReference) {
    auto target = newString("old");
    target = newString("new");
    EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 1);
}

TEST_F(Ref, ResetDeletesNow) {
    auto text = newString("reset");
    text.reset();
    EXPECT_FALSE(text);
    EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 0);
    text.reset(); // empty: nothing happens
}

TEST_F(Ref, ReleaseHandsTheReferenceOver) {
    jstring raw = nullptr;
    {
        auto text = newString("returned to java");
        raw = text.release();
        EXPECT_FALSE(text);
        EXPECT_EQ(jnirt::detail::liveLocalRefsOnThisThread(), 0);
    }
    // The destructor did not delete it: the reference is still usable.
    EXPECT_EQ(env->GetStringUTFLength(raw), 16);
    env->DeleteLocalRef(raw);
}

// ---- GlobalRef -----------------------------------------------------------------------------------------------

TEST_F(Ref, GlobalRefOutlivesTheLocalOne) {
    jnirt::GlobalRef<jstring> global;
    {
        auto local = newString("kept");
        global = jnirt::GlobalRef<jstring>(local.get());
    }
    ASSERT_TRUE(global);
    EXPECT_EQ(env->GetObjectRefType(global.get()), JNIGlobalRefType);
    EXPECT_EQ(env->GetStringUTFLength(global.get()), 4);
    EXPECT_EQ(jnirt::detail::liveGlobalRefs(), globalsAtStart + 1);
}

TEST_F(Ref, GlobalRefFromNullIsEmpty) {
    jnirt::GlobalRef<jobject> global(nullptr);
    EXPECT_FALSE(global);
    EXPECT_EQ(jnirt::detail::liveGlobalRefs(), globalsAtStart);
}

TEST_F(Ref, MovedGlobalRefIsDeletedOnce) {
    auto local = newString("one owner");
    jnirt::GlobalRef<jstring> first(local.get());
    jnirt::GlobalRef<jstring> second = std::move(first);
    EXPECT_FALSE(first);
    EXPECT_TRUE(second);
    EXPECT_EQ(jnirt::detail::liveGlobalRefs(), globalsAtStart + 1);
}

TEST_F(Ref, GlobalRefCanDieOnANativeThread) {
    auto local = newString("crosses threads");
    jnirt::GlobalRef<jstring> global(local.get());

    onNativeThread("jnirt-worker", [moved = std::move(global)]() mutable {
        JNIEnv *workerEnv = jnirt::env();
        EXPECT_EQ(workerEnv->GetStringUTFLength(moved.get()), 15);
        moved.reset(); // deleted on this thread, through its own env
    });
    EXPECT_EQ(jnirt::detail::liveGlobalRefs(), globalsAtStart);
}

TEST_F(Ref, GlobalRefDestructorAttachesAnUnknownThread) {
    auto local = newString("last owner is a pure C++ thread");
    auto global = jnirt::GlobalRef<jstring>(local.get());

    std::thread([moved = std::move(global)]() mutable {
        moved.reset(); // the thread never touched Java: the destructor attaches it, the pthread_key detaches it
    }).join();
    EXPECT_EQ(jnirt::detail::liveGlobalRefs(), globalsAtStart);
}

// ---- WeakGlobalRef -------------------------------------------------------------------------------------------

TEST_F(Ref, WeakGlobalRefPromotesWhileTheObjectIsHeld) {
    auto object = newObject();
    jnirt::WeakGlobalRef<jobject> weak(object.get());
    ASSERT_TRUE(weak);
    EXPECT_EQ(jnirt::detail::liveWeakGlobalRefs(), weaksAtStart + 1);

    auto promoted = weak.promote(env);
    ASSERT_TRUE(promoted);
    EXPECT_TRUE(env->IsSameObject(promoted.get(), object.get()));
    EXPECT_EQ(env->GetObjectRefType(promoted.get()), JNILocalRefType);
}

TEST_F(Ref, WeakGlobalRefIsEmptyAfterTheObjectIsCollected) {
    jnirt::WeakGlobalRef<jobject> weak(newObject().get()); // the only strong reference dies at the end of the line

    bool collected = collectUntil([&] { return !weak.promote(env); });
    EXPECT_TRUE(collected) << "the object was not collected after 50 System.gc() calls";
    EXPECT_TRUE(weak) << "the reference itself is still held until reset()";
}

TEST_F(Ref, GlobalRefKeepsTheObjectAlive) {
    jnirt::GlobalRef<jobject> strong(newObject().get());
    jnirt::WeakGlobalRef<jobject> weak(strong.get());

    bool collected = collectUntil([&] { return !weak.promote(env); });
    EXPECT_FALSE(collected);
}

TEST_F(Ref, EmptyWeakGlobalRefPromotesToEmpty) {
    jnirt::WeakGlobalRef<jobject> weak;
    EXPECT_FALSE(weak);
    EXPECT_FALSE(weak.promote(env));
    jnirt::WeakGlobalRef<jobject> fromNull(nullptr);
    EXPECT_FALSE(fromNull);
}

TEST_F(Ref, MovedWeakGlobalRefIsDeletedOnce) {
    auto object = newObject();
    jnirt::WeakGlobalRef<jobject> first(object.get());
    jnirt::WeakGlobalRef<jobject> second;
    second = std::move(first);
    EXPECT_FALSE(first);
    EXPECT_TRUE(second.promote(env));
    EXPECT_EQ(jnirt::detail::liveWeakGlobalRefs(), weaksAtStart + 1);
}

// ---- Debug checks --------------------------------------------------------------------------------------------

TEST_F(Ref, DetachWithALiveLocalRefCrashes) {
    EXPECT_DEATH(onNativeThread("worker-3", [] {
        JNIEnv *workerEnv = jnirt::env();
        jnirt::LocalRef<jstring> text(workerEnv, workerEnv->NewStringUTF("still here"));
        jnirt::detachCurrentThread();
    }), "detachCurrentThread\\(\\) called with 1 LocalRef still alive on thread \"worker-3\"");
}

TEST_F(Ref, DetachAfterTheScopeIsClosedIsFine) {
    onNativeThread("worker-4", [] {
        {
            JNIEnv *workerEnv = jnirt::env();
            jnirt::LocalRef<jstring> text(workerEnv, workerEnv->NewStringUTF("gone before detach"));
        }
        jnirt::detachCurrentThread();
    });
}

TEST_F(Ref, LocalRefDestroyedOnAnotherThreadCrashes) {
    EXPECT_DEATH({
        auto text = newString("wrong thread");
        onNativeThread("camera-rx", [moved = std::move(text)]() mutable {
            jnirt::env();
            moved.reset();
        });
    }, "a LocalRef is destroyed on thread \"camera-rx\", not on the thread that created it");
}
