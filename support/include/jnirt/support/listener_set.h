// A list of listeners that can be changed while it is walked. Plain C++, no JNI: core/ uses it, and it is tested on the
// host without a JVM. See docs/adr/0010-callbacks-and-subscriptions.md.
//
// In Kotlin terms: a CopyOnWriteArrayList<(Event) -> Unit> whose entries have an `active` flag.
//
//   jnirt::ListenerSet<const Event &> events;
//   jnirt::ListenerHandle handle = events.add([](const Event &e) { ... });   // throws IllegalState after close()
//   events.emit(event);          // on the thread of the source; no lock of the set is held while a listener runs
//   handle.cancel();             // from anywhere, also from inside a listener; no new call starts after it
//   events.close();              // the owner is gone: every listener is dropped, add() fails from now on
//
// What emit() promises: it walks a snapshot taken at its start. A listener added during the walk gets the next event,
// one removed during the walk is skipped if its turn has not come. A listener that throws is logged and the others
// still get the event (on a native thread there is no entry() to catch it, ADR-0007).
//
// What it can not promise: a call that has already started when cancel() is called runs to its end.
//
// Created by Yurii Surzhykov on 10/2/26.

#ifndef JNIRT_SUPPORT_LISTENER_SET_H
#define JNIRT_SUPPORT_LISTENER_SET_H

#pragma once
#include <jnirt/support/assert.h>
#include <jnirt/support/logging/log.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace jnirt {
    // The way back to one registration. It holds the list only weakly: a handle may outlive the list (the owner was
    // destroyed first), and then cancel() does nothing. Move-only; cancel() twice is fine, from two threads at once is not.
    class ListenerHandle {
    public:
        ListenerHandle() noexcept = default;

        explicit ListenerHandle(std::function<void()> cancel) : cancel_(std::move(cancel)) {
        }

        ListenerHandle(ListenerHandle &&) = default;

        ListenerHandle &operator=(ListenerHandle &&) = default;

        // Takes the listener out of its list. Does nothing when it is already out or the list is gone.
        void cancel() noexcept {
            std::function<void()> cancel = std::move(cancel_);
            cancel_ = nullptr;
            if (cancel) cancel();
        }

    private:
        std::function<void()> cancel_;
    };

    template<class... Args>
    class ListenerSet {
    public:
        using Function = std::function<void(Args...)>;

        ListenerSet() : state_(std::make_shared<State>()) {
        }

        ListenerSet(const ListenerSet &) = delete;

        ListenerSet &operator=(const ListenerSet &) = delete;

        // IllegalState after close(): a listener added to a dead owner would never be called, and would be kept for good.
        ListenerHandle add(Function function) {
            JNIRT_REQUIRE(static_cast<bool>(function), "a listener must be callable");
            std::shared_ptr<const List> replaced; // released after the lock
            std::lock_guard<std::mutex> lock(state_->mutex);
            JNIRT_CHECK(state_->list != nullptr, "the owner of this listener list is closed");
            const std::uint64_t id = state_->next++;
            auto entry = std::make_shared<Entry>(id, std::move(function));
            auto list = std::make_shared<List>(*state_->list);
            list->push_back(std::move(entry));
            replaced = std::move(state_->list);
            state_->list = std::move(list);
            return ListenerHandle([weak = std::weak_ptr<State>(state_), id] {
                if (auto state = weak.lock()) state->remove(id);
            });
        }

        void emit(Args... args) const {
            std::shared_ptr<const List> snapshot;
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                snapshot = state_->list;
            }
            if (!snapshot) return;
            for (const std::shared_ptr<Entry> &entry: *snapshot) {
                if (!entry->active.load()) continue;
                try {
                    entry->function(args...);
                } catch (const std::exception &e) {
                    logging::write(logging::Level::Error, "jnirt", "a listener threw, the others still get the event: %s",
                                   e.what());
                } catch (...) {
                    logging::write(logging::Level::Error, "jnirt",
                                   "a listener threw something that is not an exception, the others still get the event");
                }
            }
        }

        // Drops every listener now (their destructors run here, or at the end of the emit that still holds them).
        void close() noexcept {
            std::shared_ptr<const List> dropped;
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (!state_->list) return;
            for (const std::shared_ptr<Entry> &entry: *state_->list) entry->active.store(false);
            dropped = std::move(state_->list);
            state_->list = nullptr;
        }

    private:
        struct Entry {
            Entry(std::uint64_t id, Function function) : id(id), function(std::move(function)) {
            }

            const std::uint64_t id;
            Function function;
            std::atomic<bool> active{true};
        };

        using List = std::vector<std::shared_ptr<Entry> >;

        // `list` is replaced as a whole and never changed in place, which is what lets emit() walk it without a lock.
        // nullptr means closed.
        struct State {
            std::mutex mutex;
            std::shared_ptr<const List> list = std::make_shared<const List>();
            std::uint64_t next = 0;

            void remove(std::uint64_t id) noexcept {
                std::shared_ptr<const List> replaced; // released after the lock
                std::lock_guard<std::mutex> lock(mutex);
                if (!list) return;
                const auto found = std::find_if(list->begin(), list->end(),
                                                [id](const std::shared_ptr<Entry> &e) { return e->id == id; });
                if (found == list->end()) return;
                (*found)->active.store(false); // enough by itself if the copy below fails
                try {
                    auto smaller = std::make_shared<List>(*list);
                    smaller->erase(smaller->begin() + (found - list->begin()));
                    replaced = std::move(list);
                    list = std::move(smaller);
                } catch (...) {
                }
            }
        };

        std::shared_ptr<State> state_;
    };
} // namespace jnirt

#endif //JNIRT_SUPPORT_LISTENER_SET_H
