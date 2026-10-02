// ListenerSet (ADR-0010 §3): plain C++, no JVM. The stress test at the bottom is the one to run under ThreadSanitizer.

#include <jnirt/support/error.h>
#include <jnirt/support/listener_set.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using jnirt::ListenerHandle;
using jnirt::ListenerSet;

TEST(ListenerSet, EmitCallsEveryListenerInTheOrderTheyWereAdded) {
    ListenerSet<int> set;
    std::string order;
    ListenerHandle a = set.add([&](int v) { order += "a" + std::to_string(v); });
    ListenerHandle b = set.add([&](int v) { order += "b" + std::to_string(v); });
    set.emit(1);
    EXPECT_EQ(order, "a1b1");
}

TEST(ListenerSet, ACancelledListenerIsNotCalledAndCancelTwiceIsFine) {
    ListenerSet<int> set;
    int calls = 0;
    ListenerHandle handle = set.add([&](int) { ++calls; });
    set.emit(1);
    handle.cancel();
    handle.cancel();
    set.emit(2);
    EXPECT_EQ(calls, 1);
}

TEST(ListenerSet, ARunningListenerCanCancelItself) {
    ListenerSet<int> set;
    int calls = 0;
    ListenerHandle self;
    self = set.add([&](int) {
        ++calls;
        self.cancel();
    });
    set.emit(1);
    set.emit(2);
    EXPECT_EQ(calls, 1);
}

TEST(ListenerSet, ACancelInsideEmitSkipsAListenerWhoseTurnHasNotCome) {
    ListenerSet<int> set;
    int second = 0;
    ListenerHandle later;
    ListenerHandle first = set.add([&](int) { later.cancel(); });
    later = set.add([&](int) { ++second; });
    set.emit(1);
    EXPECT_EQ(second, 0);
}

TEST(ListenerSet, AListenerAddedDuringEmitGetsTheNextEventNotThisOne) {
    ListenerSet<int> set;
    int added = 0;
    ListenerHandle extra;
    bool once = true;
    ListenerHandle adder = set.add([&](int) {
        if (!once) return;
        once = false;
        extra = set.add([&](int) { ++added; });
    });
    set.emit(1);
    EXPECT_EQ(added, 0);
    set.emit(2);
    EXPECT_EQ(added, 1);
}

TEST(ListenerSet, CloseInsideEmitStopsTheRest) {
    ListenerSet<int> set;
    int second = 0;
    ListenerHandle first = set.add([&](int) { set.close(); });
    ListenerHandle later = set.add([&](int) { ++second; });
    set.emit(1);
    set.emit(2);
    EXPECT_EQ(second, 0);
}

TEST(ListenerSet, AddAfterCloseIsIllegalState) {
    ListenerSet<int> set;
    set.close();
    set.close(); // twice is fine
    EXPECT_THROW(set.add([](int) {}), jnirt::IllegalState);
    set.emit(1); // nothing happens
}

TEST(ListenerSet, ACancelAfterTheSetIsGoneDoesNothing) {
    ListenerHandle handle;
    {
        ListenerSet<int> set;
        handle = set.add([](int) {});
    }
    handle.cancel();
}

TEST(ListenerSet, ACancelAfterCloseDoesNothing) {
    ListenerSet<int> set;
    ListenerHandle handle = set.add([](int) {});
    set.close();
    handle.cancel();
}

TEST(ListenerSet, AThrowingListenerDoesNotStopTheOthers) {
    ListenerSet<int> set;
    int calls = 0;
    ListenerHandle a = set.add([](int) { throw std::runtime_error("boom"); });
    ListenerHandle b = set.add([](int) { throw 42; });
    ListenerHandle c = set.add([&](int) { ++calls; });
    set.emit(1);
    EXPECT_EQ(calls, 1);
}

TEST(ListenerSet, TheListenerIsDestroyedWhenItIsCancelledOrTheSetIsClosed) {
    ListenerSet<int> set;
    auto token = std::make_shared<int>(0);
    std::weak_ptr<int> watch = token;
    ListenerHandle handle = set.add([token](int) {});
    token.reset();
    EXPECT_FALSE(watch.expired());
    handle.cancel();
    EXPECT_TRUE(watch.expired());

    token = std::make_shared<int>(0);
    watch = token;
    ListenerHandle again = set.add([token](int) {});
    token.reset();
    set.close();
    EXPECT_TRUE(watch.expired());
}

// Emitters, subscribers that come and go, and a closer, all at once. Nothing to assert except that it ends and (under
// TSan) that there is no data race.
TEST(ListenerSet, StressEmitSubscribeCancelAndClose) {
    ListenerSet<int> set;
    std::atomic<bool> stop{false};
    std::atomic<long> calls{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&] {
            while (!stop) set.emit(1);
        });
    }
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&] {
            while (!stop) {
                try {
                    ListenerHandle handle = set.add([&](int) { ++calls; });
                    handle.cancel();
                } catch (const jnirt::IllegalState &) {
                    return; // closed
                }
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    set.close();
    stop = true;
    for (auto &t: threads) t.join();
    SUCCEED();
}
