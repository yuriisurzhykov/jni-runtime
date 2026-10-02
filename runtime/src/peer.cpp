// The table of peers (ADR-0009). Its mutex guards the map and nothing else: a hook, a destructor and a native call
// never run under it.
//
// Created by Yurii Surzhykov on 10/2/26.

#include <jnirt/runtime/peer.h>
#include <jnirt/support/error.h>

#include <mutex>
#include <string>
#include <unordered_map>

namespace jnirt {
    namespace detail {
        namespace {
            struct PeerEntry {
                std::shared_ptr<void> object;
                const void *tag = nullptr;
                PeerHook hook = nullptr;
            };

            struct PeerState {
                std::mutex mutex;
                std::unordered_map<jlong, PeerEntry> peers;
                jlong last = 0; // the last handle given; the next one is last + 1
            };

            // Never destroyed: a thread may still close a peer while the process exits (the rule of GlobalRef, ADR-0004).
            PeerState &state() {
                static PeerState *instance = new PeerState;
                return *instance;
            }

            // A violation raised by jnirt itself has no file and line: the Java stack trace names the Kotlin method.
            [[noreturn]] void throwIllegalState(const std::string &message) { throw IllegalState(message, "", 0); }
        } // namespace

        jlong addPeer(std::shared_ptr<void> object, const void *tag, PeerHook hook) {
            PeerState &table = state();
            std::lock_guard<std::mutex> lock(table.mutex);
            const jlong handle = table.last + 1;
            table.peers.emplace(handle, PeerEntry{std::move(object), tag, hook});
            table.last = handle; // after the insert: a failed insert does not use up a number
            return handle;
        }

        std::shared_ptr<void> acquirePeer(jlong handle, const void *tag) {
            PeerState &table = state();
            {
                std::lock_guard<std::mutex> lock(table.mutex);
                const auto found = table.peers.find(handle);
                if (found != table.peers.end()) {
                    if (found->second.tag == tag) return found->second.object;
                    throwIllegalState("the handle " + std::to_string(handle) +
                                      " belongs to a peer of another type.\n  fix: pass each peer to the natives of its "
                                      "own class.");
                }
            }
            throwIllegalState("the peer is closed, or " + std::to_string(handle) + " was never a peer handle.");
        }

        void nullPeer() { throwIllegalState("a native returned an empty Peer: there is nothing to give a handle to."); }
    } // namespace detail

    void PeerTable::close(jlong handle) noexcept {
        detail::PeerEntry entry;
        {
            detail::PeerState &table = detail::state();
            std::lock_guard<std::mutex> lock(table.mutex);
            const auto found = table.peers.find(handle);
            if (found == table.peers.end()) return;
            entry = std::move(found->second);
            table.peers.erase(found);
        }
        // No lock here: the hook wakes sleepers, and the destructor below may take any lock of its own.
        if (entry.hook != nullptr) entry.hook(entry.object.get());
    }

    std::size_t PeerTable::size() noexcept {
        detail::PeerState &table = detail::state();
        std::lock_guard<std::mutex> lock(table.mutex);
        return table.peers.size();
    }
} // namespace jnirt
