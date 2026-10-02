// Peer<T> as a parameter and as a result of a native (ADR-0009 §4), and the native close() that Kotlin calls,
// against a real JVM (-Xcheck:jni). The table itself is tested in peer_table_test.cpp.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/native_buffer.h>
#include <jnirt/runtime/peer.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/runtime/signature.h>
#include <jnirt/support/error.h>

#include "class_cache_internal.h"
#include "ref_internal.h"
#include "registry_internal.h"
#include "runtime_classes.h"
#include "test_jvm.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

using namespace jnirt;

namespace {
    // ---- the native object ----

    class Cam {
    public:
        static inline std::atomic<int> alive{0};
        static inline std::atomic<int> entered{0};

        explicit Cam(std::string name) : name_(std::move(name)) {
            if (name_ == "boom") throw std::runtime_error("no such camera");
            ++alive;
        }

        ~Cam() { --alive; }

        const std::string &name() const { return name_; }

        void block() {
            std::unique_lock<std::mutex> lock(mutex_);
            ++entered;
            cv_.wait(lock, [this] { return closed_; });
            throw IllegalState("camera closed", "", 0);
        }

        void onClose() noexcept {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
            }
            cv_.notify_all();
        }

    private:
        std::string name_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool closed_ = false;
    };

    struct Dog {
    };

    // ---- compile time ----

    static_assert(std::string_view(descriptor<Peer<Cam> >) == "J");
    static_assert(std::string_view(signature<Peer<Cam>(const std::string &)>) == "(Ljava/lang/String;)J");
    static_assert(std::string_view(signature<std::string(Peer<Cam>)>) == "(J)Ljava/lang/String;");
    static_assert(std::is_same_v<detail::JniOf<Peer<Cam> >, jlong>);
    static_assert(!detail::rejectsNull<Peer<Cam> >); // a Long is never null

    // ---- the natives ----

    Peer<Cam> openCam(JNIEnv *, jclass, const std::string &name) { return makePeer<Cam>(name); }

    Peer<Cam> openFailing(JNIEnv *, jclass) { return makePeer<Cam>("boom"); }

    Peer<Cam> openEmpty(JNIEnv *, jclass) { return Peer<Cam>(); }

    std::string nameOf(JNIEnv *, jclass, Peer<Cam> self) { return self->name(); }

    std::string nameOfOther(JNIEnv *, jclass, Peer<Dog>) { return "dog"; }

    void block(JNIEnv *, jclass, Peer<Cam> self) { self->block(); }

    NativeBuffer frame(JNIEnv *, jclass, jint size, jint fill) {
        NativeBuffer buffer = NativeBuffer::allocate(static_cast<std::size_t>(size));
        for (jint i = 0; i < size; ++i) buffer.data()[i] = static_cast<std::byte>(fill);
        return buffer;
    }

    NativeBuffer nameBytes(JNIEnv *, jclass, Peer<Cam> self) {
        return NativeBuffer::copyOf(self->name().data(), self->name().size());
    }

    // Memory that says when it is released.
    struct Counted {
        static inline std::atomic<int> alive{0};
        std::byte bytes[16]{};

        Counted() { ++alive; }

        ~Counted() { --alive; }
    };

    NativeBuffer countedFrame(JNIEnv *, jclass) {
        auto memory = std::make_shared<Counted>();
        return NativeBuffer(memory, memory->bytes, sizeof(memory->bytes));
    }

    jint countedAlive(JNIEnv *, jclass) { return Counted::alive; }

    NativeBuffer emptyFrame(JNIEnv *, jclass) { return NativeBuffer(); }

    struct ByteBufferJ : JavaClass<ByteBufferJ> {
        static constexpr const char *name = "java/nio/ByteBuffer";
    };

    struct PeersJ : JavaClass<PeersJ> {
        static constexpr const char *name = "jnirt/test/Peers";

        static inline StaticMethod<PeersJ, jlong(std::string)> open{"open"};
        static inline StaticMethod<PeersJ, jlong()> openFailing{"openFailing"};
        static inline StaticMethod<PeersJ, jlong()> openEmpty{"openEmpty"};
        static inline StaticMethod<PeersJ, std::string(jlong)> nameOf{"nameOf"};
        static inline StaticMethod<PeersJ, std::string(jlong)> nameOfOther{"nameOfOther"};
        static inline StaticMethod<PeersJ, void(jlong)> block{"block"};
        static inline StaticMethod<PeersJ, ByteBufferJ(jint, jint)> frame{"frame"};
        static inline StaticMethod<PeersJ, ByteBufferJ(jlong)> nameBytes{"nameBytes"};
        static inline StaticMethod<PeersJ, ByteBufferJ()> countedFrame{"countedFrame"};
        static inline StaticMethod<PeersJ, jint()> countedAlive{"countedAlive"};
        static inline StaticMethod<PeersJ, ByteBufferJ()> emptyFrame{"emptyFrame"};
        static inline StaticMethod<PeersJ, jboolean()> collectUntilGone{"collectUntilGone"};
        static inline StaticMethod<PeersJ, void()> gc{"gc"};
    };

    // What NativePeer.close() calls: the native that init() registers itself (the Java stand-in is
    // tests/host/java/jnirt/NativePeerNatives.java). Called by hand, since it is not one of the test's own classes.
    struct NativePeerNativesCallJ {
        static void close(JNIEnv *env, jlong handle) {
            jclass cls = env->FindClass("jnirt/NativePeerNatives");
            jmethodID method = env->GetStaticMethodID(cls, "close", "(J)V");
            env->CallStaticVoidMethod(cls, method, handle);
            env->DeleteLocalRef(cls);
        }
    };

    void registerPeers(Registry &r) {
        r.add<PeersJ>()
                .native<&openCam>("open")
                .native<&openFailing>("openFailing")
                .native<&openEmpty>("openEmpty")
                .native<&nameOf>("nameOf")
                .native<&nameOfOther>("nameOfOther")
                .native<&block>("block")
                .native<&frame>("frame")
                .native<&nameBytes>("nameBytes")
                .native<&countedFrame>("countedFrame")
                .native<&countedAlive>("countedAlive")
                .native<&emptyFrame>("emptyFrame");
        r.add<ByteBufferJ>(); // described only so that it can be a result type
    }

    class PeerConvertTest : public ::testing::Test {
    protected:
        void SetUp() override {
            env = jnirt::env();
            Registry registry;
            registerPeers(registry);
            auto problems = detail::startup(env, registry, {&detail::classInfo<PeersJ>()});
            ASSERT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0]);
            Cam::alive = 0;
            Cam::entered = 0;
            openBefore = PeerTable::size();
            localRefsBefore = detail::liveLocalRefsOnThisThread();
        }

        void TearDown() override {
            EXPECT_FALSE(env->ExceptionCheck());
            EXPECT_EQ(detail::liveLocalRefsOnThisThread(), localRefsBefore) << "local references leaked";
            EXPECT_EQ(PeerTable::size(), openBefore) << "a peer stayed in the table";
            EXPECT_EQ(Cam::alive, 0) << "a native object was not destroyed";
        }

        template<class F>
        std::string javaFailure(F &&call) {
            try {
                call();
            } catch (const JavaException &e) {
                return e.what();
            }
            return "no exception";
        }

        // The memory of a ByteBuffer is in the table until the garbage collector takes the buffer: ask for it.
        void collectBuffers() {
            for (int i = 0; i < 200 && PeerTable::size() > openBefore; ++i) {
                PeersJ::gc(env);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        static bool contains(const std::string &whole, const std::string &part) {
            return whole.find(part) != std::string::npos;
        }

        JNIEnv *env = nullptr;
        std::size_t openBefore = 0;
        int localRefsBefore = 0;
    };

    TEST_F(PeerConvertTest, AResultGetsAHandleAndAParameterFindsTheObject) {
        const jlong handle = PeersJ::open(env, "front door");
        ASSERT_NE(handle, 0);
        EXPECT_EQ(PeerTable::size(), openBefore + 1);
        EXPECT_EQ(PeersJ::nameOf(env, handle), "front door");
        EXPECT_EQ(PeersJ::nameOf(env, handle), "front door"); // as often as you like
        NativePeerNativesCallJ::close(env, handle);
    }

    TEST_F(PeerConvertTest, CloseFromKotlinReleasesTheObjectAndTheHandle) {
        const jlong handle = PeersJ::open(env, "x");
        EXPECT_EQ(Cam::alive, 1);
        NativePeerNativesCallJ::close(env, handle);
        EXPECT_EQ(Cam::alive, 0);
        NativePeerNativesCallJ::close(env, handle); // twice is fine
        NativePeerNativesCallJ::close(env, 0);      // so is a handle that never existed
    }

    TEST_F(PeerConvertTest, ACallAfterCloseIsAnIllegalStateExceptionInJava) {
        const jlong handle = PeersJ::open(env, "x");
        NativePeerNativesCallJ::close(env, handle);
        const std::string failure = javaFailure([&] { PeersJ::nameOf(env, handle); });
        EXPECT_TRUE(contains(failure, "java.lang.IllegalStateException")) << failure;
        EXPECT_TRUE(contains(failure, "closed")) << failure;
    }

    TEST_F(PeerConvertTest, ZeroAndInventedHandlesAreIllegalState) {
        EXPECT_TRUE(contains(javaFailure([&] { PeersJ::nameOf(env, 0); }), "IllegalStateException"));
        EXPECT_TRUE(contains(javaFailure([&] { PeersJ::nameOf(env, 424242); }), "IllegalStateException"));
    }

    TEST_F(PeerConvertTest, AHandleOfAnotherTypeIsIllegalState) {
        const jlong handle = PeersJ::open(env, "x");
        const std::string failure = javaFailure([&] { PeersJ::nameOfOther(env, handle); });
        EXPECT_TRUE(contains(failure, "IllegalStateException")) << failure;
        EXPECT_TRUE(contains(failure, "another type")) << failure;
        NativePeerNativesCallJ::close(env, handle);
    }

    TEST_F(PeerConvertTest, AConstructorThatThrowsGivesNoHandle) {
        const std::string failure = javaFailure([&] { PeersJ::openFailing(env); });
        EXPECT_TRUE(contains(failure, "no such camera")) << failure;
        EXPECT_EQ(PeerTable::size(), openBefore);
    }

    TEST_F(PeerConvertTest, AnEmptyPeerAsAResultIsAnError) {
        const std::string failure = javaFailure([&] { PeersJ::openEmpty(env); });
        EXPECT_TRUE(contains(failure, "IllegalStateException")) << failure;
        EXPECT_TRUE(contains(failure, "empty Peer")) << failure;
    }

    TEST_F(PeerConvertTest, CloseFromAnotherThreadWakesABlockedNativeCall) {
        const jlong handle = PeersJ::open(env, "x");
        std::string failure;
        std::thread reader([&] {
            JNIEnv *threadEnv = jnirt::env(); // attaches this thread
            try {
                PeersJ::block(threadEnv, handle);
            } catch (const JavaException &e) {
                failure = e.what();
            }
        });
        while (Cam::entered == 0) std::this_thread::yield();

        NativePeerNativesCallJ::close(env, handle); // does not wait for the call
        reader.join();

        EXPECT_TRUE(contains(failure, "java.lang.IllegalStateException")) << failure;
        EXPECT_TRUE(contains(failure, "camera closed")) << failure;
        // TearDown: the object is gone, the handle is gone.
    }
    // ---- NativeBuffer: native memory as a direct ByteBuffer (ADR-0009 §9) ----

    static_assert(std::string_view(descriptor<NativeBuffer>) == "Ljava/nio/ByteBuffer;");
    static_assert(std::string_view(signature<NativeBuffer(jint, jint)>) == "(II)Ljava/nio/ByteBuffer;");

    TEST_F(PeerConvertTest, ANativeBufferIsADirectByteBufferOverTheSameBytes) {
        {
            LocalRef<jobject> buffer = PeersJ::frame(env, 5, 0x7A);
            ASSERT_TRUE(buffer);
            auto *bytes = static_cast<unsigned char *>(env->GetDirectBufferAddress(buffer.get()));
            ASSERT_NE(bytes, nullptr) << "not a direct buffer";
            EXPECT_EQ(env->GetDirectBufferCapacity(buffer.get()), 5);
            for (int i = 0; i < 5; ++i) EXPECT_EQ(bytes[i], 0x7A);
        }
        collectBuffers();
    }

    TEST_F(PeerConvertTest, ABufferOfNoBytesIsAnEmptyByteBuffer) {
        {
            LocalRef<jobject> buffer = PeersJ::emptyFrame(env);
            ASSERT_TRUE(buffer);
            EXPECT_EQ(env->GetDirectBufferCapacity(buffer.get()), 0);
        }
        collectBuffers();
    }

    TEST_F(PeerConvertTest, TheMemoryOutlivesThePeerThatMadeIt) {
        const jlong handle = PeersJ::open(env, "garage");
        {
            LocalRef<jobject> buffer = PeersJ::nameBytes(env, handle);
            NativePeerNativesCallJ::close(env, handle); // the camera is gone, the frame is not
            EXPECT_EQ(Cam::alive, 0);
            const char *bytes = static_cast<const char *>(env->GetDirectBufferAddress(buffer.get()));
            ASSERT_NE(bytes, nullptr);
            EXPECT_EQ(std::string(bytes, 6), "garage");
        }
        collectBuffers();
    }

    TEST_F(PeerConvertTest, TheMemoryIsReleasedWhenTheGarbageCollectorTakesTheBuffer) {
        ASSERT_EQ(Counted::alive, 0);
        {
            LocalRef<jobject> buffer = PeersJ::countedFrame(env);
            ASSERT_TRUE(buffer);
            EXPECT_EQ(Counted::alive, 1);
            System_gc_while_held:
            EXPECT_EQ(PeersJ::countedAlive(env), 1) << "alive while Kotlin holds the buffer";
        }
        // The local reference is gone: only the collector keeps the memory now.
        EXPECT_TRUE(PeersJ::collectUntilGone(env) == JNI_TRUE) << "the memory was never released";
        EXPECT_EQ(Counted::alive, 0);
        collectBuffers();
    }
} // namespace
