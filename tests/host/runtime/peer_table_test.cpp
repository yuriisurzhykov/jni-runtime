// The table of peers (ADR-0009): handles, the hook, the destructor, and close() against calls that are in flight.
// No JVM: the table is plain C++. The Peer<T> row against a real JVM is in peer_convert_test.cpp.

#include <jnirt/runtime/peer.h>
#include <jnirt/support/error.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace jnirt;

namespace {
    // Counts what happens to it, in order.
    struct Probe {
        static inline std::atomic<int> alive{0};
        static inline std::atomic<int> destroyed{0};
        static inline std::atomic<int> hooks{0};
        static inline std::atomic<int> hooksBeforeDestroy{0};

        Probe() { ++alive; }

        ~Probe() {
            if (closed) ++hooksBeforeDestroy;
            --alive;
            ++destroyed;
        }

        void onClose() noexcept {
            closed = true;
            ++hooks;
        }

        std::atomic<bool> closed{false};

        static void reset() {
            alive = 0;
            destroyed = 0;
            hooks = 0;
            hooksBeforeDestroy = 0;
        }
    };

    struct Other {
    };

    // Uses the name `close()` for the hook.
    struct ClosesByThatName {
        static inline std::atomic<int> closes{0};

        void close() noexcept { ++closes; }
    };

    // Has both: onClose wins.
    struct HasBoth {
        static inline std::atomic<int> onCloses{0};
        static inline std::atomic<int> closes{0};

        void onClose() noexcept { ++onCloses; }

        void close() noexcept { ++closes; }
    };

    // A camera that blocks in read() until it is woken, like recv() on a socket.
    class BlockingCamera {
    public:
        // Returns once close() woke it. Throws IllegalState like a real read() would.
        void read() {
            std::unique_lock<std::mutex> lock(mutex_);
            ++waiting_;
            cv_.wait(lock, [this] { return closed_; });
            --waiting_;
            throw IllegalState("camera closed", "", 0);
        }

        void onClose() noexcept {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
            }
            cv_.notify_all();
        }

        int waiting() {
            std::lock_guard<std::mutex> lock(mutex_);
            return waiting_;
        }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        bool closed_ = false;
        int waiting_ = 0;
    };

    class PeerTableTest : public ::testing::Test {
    protected:
        void SetUp() override {
            Probe::reset();
            ClosesByThatName::closes = 0;
            HasBoth::onCloses = 0;
            HasBoth::closes = 0;
            openBefore = PeerTable::size();
        }

        void TearDown() override { EXPECT_EQ(PeerTable::size(), openBefore) << "a peer stayed in the table"; }

        std::size_t openBefore = 0;
    };

    // ---- handles ----

    TEST_F(PeerTableTest, AddGivesAHandleThatAcquireFindsAgain) {
        auto object = std::make_shared<Probe>();
        const jlong handle = PeerTable::add(object);
        EXPECT_NE(handle, 0);
        EXPECT_EQ(PeerTable::acquire<Probe>(handle).get(), object.get());
        PeerTable::close(handle);
    }

    TEST_F(PeerTableTest, HandlesAreNeverReused) {
        std::set<jlong> seen;
        for (int i = 0; i < 100; ++i) {
            const jlong handle = PeerTable::add(std::make_shared<Probe>());
            EXPECT_TRUE(seen.insert(handle).second) << "handle " << handle << " came twice";
            PeerTable::close(handle);
        }
    }

    TEST_F(PeerTableTest, ZeroAndUnknownHandlesAreIllegalState) {
        EXPECT_THROW(PeerTable::acquire<Probe>(0), IllegalState);
        EXPECT_THROW(PeerTable::acquire<Probe>(123456789), IllegalState);
        EXPECT_THROW(PeerTable::acquire<Probe>(-1), IllegalState);
    }

    TEST_F(PeerTableTest, AHandleOfAnotherTypeIsIllegalStateAndNotAWrongCast) {
        const jlong handle = PeerTable::add(std::make_shared<Probe>());
        EXPECT_THROW(PeerTable::acquire<Other>(handle), IllegalState);
        EXPECT_NO_THROW(PeerTable::acquire<Probe>(handle)); // the mistake did not close it
        PeerTable::close(handle);
    }

    TEST_F(PeerTableTest, AClosedHandleIsIllegalStateForever) {
        const jlong handle = PeerTable::add(std::make_shared<Probe>());
        PeerTable::close(handle);
        EXPECT_THROW(PeerTable::acquire<Probe>(handle), IllegalState);
        const jlong next = PeerTable::add(std::make_shared<Probe>()); // a new object does not take the old number
        EXPECT_NE(next, handle);
        EXPECT_THROW(PeerTable::acquire<Probe>(handle), IllegalState);
        PeerTable::close(next);
    }

    TEST_F(PeerTableTest, TheMessageHasNoPlaceInJnirt) {
        try {
            PeerTable::acquire<Probe>(777);
            FAIL() << "no exception";
        } catch (const IllegalState &e) {
            EXPECT_STREQ(e.file(), "");
            EXPECT_NE(std::string(e.what()).find("777"), std::string::npos);
        }
    }

    // ---- close() ----

    TEST_F(PeerTableTest, CloseTwiceDoesNothingTheSecondTime) {
        const jlong handle = PeerTable::add(std::make_shared<Probe>());
        PeerTable::close(handle);
        PeerTable::close(handle);
        PeerTable::close(0);
        PeerTable::close(999999);
        EXPECT_EQ(Probe::hooks, 1);
        EXPECT_EQ(Probe::destroyed, 1);
    }

    TEST_F(PeerTableTest, TheHookRunsOnceBeforeTheDestructor) {
        const jlong handle = PeerTable::add(std::make_shared<Probe>());
        PeerTable::close(handle);
        EXPECT_EQ(Probe::hooks, 1);
        EXPECT_EQ(Probe::hooksBeforeDestroy, 1);
        EXPECT_EQ(Probe::alive, 0);
    }

    TEST_F(PeerTableTest, ACallInFlightKeepsTheMemoryButNotTheResource) {
        const jlong handle = PeerTable::add(std::make_shared<Probe>());
        Peer<Probe> inFlight = PeerTable::acquire<Probe>(handle);

        PeerTable::close(handle);
        EXPECT_EQ(Probe::hooks, 1) << "the resource is released at once";
        EXPECT_EQ(Probe::alive, 1) << "the object lives while a call holds it";
        EXPECT_TRUE(inFlight->closed);
        EXPECT_THROW(PeerTable::acquire<Probe>(handle), IllegalState) << "new calls fail at once";

        inFlight = Peer<Probe>();
        EXPECT_EQ(Probe::alive, 0);
        EXPECT_EQ(Probe::destroyed, 1) << "destroyed once, by the last call";
    }

    TEST_F(PeerTableTest, AClassWithoutAHookIsStillClosedAndDestroyed) {
        const jlong handle = PeerTable::add(std::make_shared<Other>());
        PeerTable::close(handle);
        EXPECT_THROW(PeerTable::acquire<Other>(handle), IllegalState);
    }

    TEST_F(PeerTableTest, CloseCanBeTheNameOfTheHook) {
        PeerTable::close(PeerTable::add(std::make_shared<ClosesByThatName>()));
        EXPECT_EQ(ClosesByThatName::closes, 1);
    }

    TEST_F(PeerTableTest, OnCloseWinsOverClose) {
        PeerTable::close(PeerTable::add(std::make_shared<HasBoth>()));
        EXPECT_EQ(HasBoth::onCloses, 1);
        EXPECT_EQ(HasBoth::closes, 0);
    }

    // The hook and the destructor run with no lock held: they may use the table themselves.
    struct UsesTheTable {
        static inline jlong other = 0;
        static inline bool sawIllegalState = false;

        void onClose() noexcept {
            try {
                PeerTable::acquire<UsesTheTable>(other); // would deadlock if close() held the mutex
            } catch (const IllegalState &) {
                sawIllegalState = true;
            }
            PeerTable::close(other);
        }
    };

    TEST_F(PeerTableTest, TheHookMayCallTheTable) {
        const jlong first = PeerTable::add(std::make_shared<UsesTheTable>());
        UsesTheTable::other = PeerTable::add(std::make_shared<UsesTheTable>());
        PeerTable::close(UsesTheTable::other); // closes itself, whose hook calls the table: no deadlock
        PeerTable::close(first);               // its hook finds `other` already closed
        EXPECT_TRUE(UsesTheTable::sawIllegalState);
    }

    // ---- close() against calls in flight ----

    TEST_F(PeerTableTest, CloseWakesABlockedCallAtOnce) {
        const jlong handle = PeerTable::add(std::make_shared<BlockingCamera>());
        std::atomic<bool> threw{false};

        std::thread reader([&] {
            Peer<BlockingCamera> camera = PeerTable::acquire<BlockingCamera>(handle);
            try {
                camera->read(); // blocks
            } catch (const IllegalState &) {
                threw = true;
            }
        });
        Peer<BlockingCamera> probe = PeerTable::acquire<BlockingCamera>(handle);
        while (probe->waiting() == 0) std::this_thread::yield();

        const auto before = std::chrono::steady_clock::now();
        PeerTable::close(handle); // must not wait for read()
        const auto took = std::chrono::steady_clock::now() - before;
        EXPECT_LT(took, std::chrono::seconds(1));

        reader.join(); // woken by the hook, not by luck
        EXPECT_TRUE(threw);
        EXPECT_THROW(PeerTable::acquire<BlockingCamera>(handle), IllegalState);
    }

    TEST_F(PeerTableTest, ManyCallersAndOneCloseDestroyTheObjectExactlyOnce) {
        for (int round = 0; round < 200; ++round) {
            Probe::reset();
            const jlong handle = PeerTable::add(std::make_shared<Probe>());
            std::atomic<bool> go{false};
            std::atomic<int> ok{0};
            std::atomic<int> refused{0};

            std::vector<std::thread> callers;
            for (int i = 0; i < 4; ++i) {
                callers.emplace_back([&] {
                    while (!go) std::this_thread::yield();
                    for (int j = 0; j < 50; ++j) {
                        try {
                            Peer<Probe> p = PeerTable::acquire<Probe>(handle);
                            ++ok;
                        } catch (const IllegalState &) {
                            ++refused;
                        }
                    }
                });
            }
            std::vector<std::thread> closers;
            for (int i = 0; i < 2; ++i) {
                closers.emplace_back([&] {
                    while (!go) std::this_thread::yield();
                    PeerTable::close(handle); // two threads close the same handle
                });
            }
            go = true;
            for (auto &t: callers) t.join();
            for (auto &t: closers) t.join();

            ASSERT_EQ(ok + refused, 200);
            ASSERT_EQ(Probe::hooks, 1) << "round " << round;
            ASSERT_EQ(Probe::destroyed, 1) << "round " << round;
            ASSERT_EQ(Probe::hooksBeforeDestroy, 1) << "round " << round;
        }
    }

    TEST_F(PeerTableTest, ManyThreadsAddAndCloseTheirOwnPeers) {
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([] {
                for (int j = 0; j < 500; ++j) {
                    const jlong handle = PeerTable::add(std::make_shared<Probe>());
                    PeerTable::acquire<Probe>(handle);
                    PeerTable::close(handle);
                }
            });
        }
        for (auto &t: threads) t.join();
        EXPECT_EQ(Probe::alive, 0);
        EXPECT_EQ(Probe::destroyed, 8 * 500);
    }

    // ---- Peer ----

    TEST_F(PeerTableTest, APeerIsLikeAPointer) {
        Peer<Probe> empty;
        EXPECT_FALSE(empty);
        auto object = std::make_shared<Probe>();
        Peer<Probe> peer(object);
        EXPECT_TRUE(peer);
        EXPECT_EQ(peer.get(), object.get());
        EXPECT_EQ(&*peer, object.get());
        EXPECT_EQ(&peer->closed, &object->closed);
    }

    TEST_F(PeerTableTest, MakePeerBuildsTheObjectWithoutAHandle) {
        {
            Peer<Probe> peer = makePeer<Probe>();
            EXPECT_EQ(Probe::alive, 1);
            EXPECT_EQ(PeerTable::size(), openBefore) << "not in the table until it is a result";
        }
        EXPECT_EQ(Probe::alive, 0);
    }
} // namespace
