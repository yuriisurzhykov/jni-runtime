// Listener<J>, subscriptions and ListenerSet against a real JVM (-Xcheck:jni) (ADR-0010): Java listeners called from the
// calling thread and from a native thread, a listener that throws, the global references that must go back to the count
// they started from, and NativeBuffer as an argument of a call into Java.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/listener.h>
#include <jnirt/runtime/native_buffer.h>
#include <jnirt/runtime/peer.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/runtime/subscription.h>
#include <jnirt/support/listener_set.h>

#include "class_cache_internal.h"
#include "ref_internal.h"
#include "registry_internal.h"
#include "runtime_classes.h"
#include "test_jvm.h"

#include <chrono>
#include <string>
#include <thread>

using namespace jnirt;

namespace {
    struct Event {
        bool frame = false;
        std::string text;
        int size = 0;
        int fill = 0;
    };

    class Source {
    public:
        ListenerSet<const Event &> events;

        void onClose() noexcept { events.close(); }
    };

    struct SinkJ : JavaClass<SinkJ> {
        static constexpr const char *name = "jnirt/test/Sink";

        static inline Method<SinkJ, void(std::string)> onText{"onText"};
        static inline Method<SinkJ, void(NativeBuffer)> onFrame{"onFrame"};
    };

    struct RecorderJ : JavaClass<RecorderJ> {
        static constexpr const char *name = "jnirt/test/Recorder";

        static inline Method<RecorderJ, jint()> texts{"texts"};
        static inline Method<RecorderJ, jint()> frames{"frames"};
        static inline Method<RecorderJ, jint()> lastSize{"lastSize"};
        static inline Method<RecorderJ, jint()> lastFirst{"lastFirst"};
    };

    Peer<Source> open(JNIEnv *, jclass) { return makePeer<Source>(); }

    NativeBuffer filled(int size, int fill) {
        NativeBuffer buffer = NativeBuffer::allocate(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i) buffer.data()[i] = static_cast<std::byte>(fill);
        return buffer;
    }

    Peer<Subscription> subscribe(JNIEnv *, jclass, Peer<Source> self, Listener<SinkJ> sink) {
        return makeSubscription(self->events.add([sink](const Event &e) {
            if (e.frame) {
                sink.call(SinkJ::onFrame, filled(e.size, e.fill));
            } else {
                sink.call(SinkJ::onText, e.text);
            }
        }));
    }

    void emit(JNIEnv *, jclass, Peer<Source> self, const std::string &text) {
        Event e;
        e.text = text;
        self->events.emit(e);
    }

    void emitFrame(JNIEnv *, jclass, Peer<Source> self, jint size, jint fill) {
        Event e;
        e.frame = true;
        e.size = size;
        e.fill = fill;
        self->events.emit(e);
    }

    jint emitFromThread(JNIEnv *, jclass, Peer<Source> self, jint count) {
        int liveAtTheEnd = -1;
        std::thread worker([&] {
            Event e;
            e.text = "tick";
            for (jint i = 0; i < count; ++i) self->events.emit(e); // the first call attaches this thread
            liveAtTheEnd = detail::liveLocalRefsOnThisThread();
        });
        worker.join();
        return liveAtTheEnd;
    }

    struct SubsJ : JavaClass<SubsJ> {
        static constexpr const char *name = "jnirt/test/Subs";

        static inline StaticMethod<SubsJ, jlong()> open{"open"};
        static inline StaticMethod<SubsJ, jlong(jlong, SinkJ)> subscribe{"subscribe"};
        static inline StaticMethod<SubsJ, void(jlong, std::string)> emit{"emit"};
        static inline StaticMethod<SubsJ, void(jlong, jint, jint)> emitFrame{"emitFrame"};
        static inline StaticMethod<SubsJ, jint(jlong, jint)> emitFromThread{"emitFromThread"};
        static inline StaticMethod<SubsJ, RecorderJ()> newRecorder{"newRecorder"};
        static inline StaticMethod<SubsJ, RecorderJ()> failingRecorder{"failingRecorder"};
        static inline StaticMethod<SubsJ, void()> gc{"gc"};
    };

    // The counters of references exist in debug builds only (ADR-0004 §8): in a release build every count is 0.
    std::size_t globalRefs() {
#ifndef NDEBUG
        return detail::liveGlobalRefs();
#else
        return 0;
#endif
    }

    // What NativePeer.close() calls (the Java stand-in is tests/host/java/jnirt/NativePeerNatives.java).
    void closeHandle(JNIEnv *env, jlong handle) {
        jclass cls = env->FindClass("jnirt/NativePeerNatives");
        jmethodID method = env->GetStaticMethodID(cls, "close", "(J)V");
        env->CallStaticVoidMethod(cls, method, handle);
        env->DeleteLocalRef(cls);
    }

    class ListenerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            env = jnirt::env();
            Registry registry;
            registry.add<SubsJ>()
                    .native<&open>("open")
                    .native<&subscribe>("subscribe")
                    .native<&emit>("emit")
                    .native<&emitFrame>("emitFrame")
                    .native<&emitFromThread>("emitFromThread");
            registry.add<SinkJ>();
            registry.add<RecorderJ>();
            auto problems = detail::startup(env, registry, {&detail::classInfo<SubsJ>()});
            ASSERT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0]);
            openBefore = PeerTable::size();
            globalsBefore = globalRefs();
            localsBefore = detail::liveLocalRefsOnThisThread();
        }

        void TearDown() override {
            EXPECT_FALSE(env->ExceptionCheck());
            // The memory of the frames is in the table until the garbage collector takes the ByteBuffers.
            for (int i = 0; i < 200 && PeerTable::size() > openBefore; ++i) {
                SubsJ::gc(env);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            EXPECT_EQ(PeerTable::size(), openBefore) << "a peer stayed in the table";
            EXPECT_EQ(globalRefs(), globalsBefore) << "a global reference of a listener leaked";
            EXPECT_EQ(detail::liveLocalRefsOnThisThread(), localsBefore) << "local references leaked";
        }

        JNIEnv *env = nullptr;
        std::size_t openBefore = 0;
        std::size_t globalsBefore = 0;
        int localsBefore = 0;
    };

    TEST_F(ListenerTest, EventsReachTheJavaListenerUntilTheSubscriptionIsClosed) {
        const jlong source = SubsJ::open(env);
        auto recorder = SubsJ::newRecorder(env);
        const jlong subscription = SubsJ::subscribe(env, source, recorder.get());
        EXPECT_EQ(globalRefs(), globalsBefore + (globalRefs() == 0 ? 0 : 1)); // the listener is held strongly (debug builds)
        SubsJ::emit(env, source, "a");
        SubsJ::emit(env, source, "b");
        EXPECT_EQ(RecorderJ::texts(env, recorder.get()), 2);

        closeHandle(env, subscription);
        SubsJ::emit(env, source, "c");
        EXPECT_EQ(RecorderJ::texts(env, recorder.get()), 2);
        EXPECT_EQ(globalRefs(), globalsBefore); // the reference went with the registration
        closeHandle(env, subscription); // twice is fine
        closeHandle(env, source);
    }

    TEST_F(ListenerTest, ClosingTheOwnerDropsTheListenersAtOnce) {
        const jlong source = SubsJ::open(env);
        auto recorder = SubsJ::newRecorder(env);
        const jlong subscription = SubsJ::subscribe(env, source, recorder.get());
        closeHandle(env, source);
        EXPECT_EQ(globalRefs(), globalsBefore);
        closeHandle(env, subscription); // the subscription outlived its owner: nothing happens
        EXPECT_EQ(RecorderJ::texts(env, recorder.get()), 0);
    }

    TEST_F(ListenerTest, AJavaListenerThatThrowsDoesNotStopTheOthers) {
        const jlong source = SubsJ::open(env);
        auto failing = SubsJ::failingRecorder(env);
        auto fine = SubsJ::newRecorder(env);
        const jlong first = SubsJ::subscribe(env, source, failing.get());
        const jlong second = SubsJ::subscribe(env, source, fine.get());
        EXPECT_NO_THROW(SubsJ::emit(env, source, "x")); // no JavaException comes out of the native thread's emit
        EXPECT_EQ(RecorderJ::texts(env, failing.get()), 1);
        EXPECT_EQ(RecorderJ::texts(env, fine.get()), 1);
        closeHandle(env, first);
        closeHandle(env, second);
        closeHandle(env, source);
    }

    TEST_F(ListenerTest, ANativeThreadCallsJavaAndItsLocalReferencesStayFlat) {
        const jlong source = SubsJ::open(env);
        auto recorder = SubsJ::newRecorder(env);
        const jlong subscription = SubsJ::subscribe(env, source, recorder.get());
        EXPECT_EQ(SubsJ::emitFromThread(env, source, 5000), 0); // 5000 events, no local reference left behind
        EXPECT_EQ(RecorderJ::texts(env, recorder.get()), 5000);
        closeHandle(env, subscription);
        closeHandle(env, source);
    }

    TEST_F(ListenerTest, ANativeBufferCanBeAnArgumentOfACallIntoJava) {
        const jlong source = SubsJ::open(env);
        auto recorder = SubsJ::newRecorder(env);
        const jlong subscription = SubsJ::subscribe(env, source, recorder.get());
        SubsJ::emitFrame(env, source, 5, 7);
        EXPECT_EQ(RecorderJ::frames(env, recorder.get()), 1);
        EXPECT_EQ(RecorderJ::lastSize(env, recorder.get()), 5);
        EXPECT_EQ(RecorderJ::lastFirst(env, recorder.get()), 7);
        closeHandle(env, subscription);
        closeHandle(env, source);
    }
} // namespace
