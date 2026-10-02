// A subscription is a peer (ADR-0009 §7, ADR-0010 §4): its native part is one registration in a ListenerSet, and its
// onClose() takes the registration out. Closing it from Kotlin is the same NativePeer.close() as for any peer.
//
//   class Camera {
//   public:
//       jnirt::ListenerHandle subscribe(std::function<void(const Event &)> f) { return events_.add(std::move(f)); }
//       void onClose() noexcept { events_.close(); ... }          // the owner closes the whole list
//   private:
//       jnirt::ListenerSet<const Event &> events_;
//   };
//
//   Peer<Subscription> subscribe(JNIEnv *, jclass, Peer<Camera> self, Listener<FrameListenerJ> listener) {
//       return makeSubscription(self->subscribe(...));              // Kotlin: Long, wrapped by jnirt.Subscription
//   }
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_RUNTIME_SUBSCRIPTION_H
#define JNI_RUNTIME_SUBSCRIPTION_H

#pragma once
#include <jnirt/runtime/peer.h>
#include <jnirt/support/listener_set.h>

#include <utility>

namespace jnirt {
    class Subscription {
    public:
        explicit Subscription(ListenerHandle handle) : handle_(std::move(handle)) {
        }

        // Called by PeerTable::close(): no new call of the listener starts. Does not wait for one that is running.
        void onClose() noexcept { handle_.cancel(); }

    private:
        ListenerHandle handle_;
    };

    inline Peer<Subscription> makeSubscription(ListenerHandle handle) {
        return makePeer<Subscription>(std::move(handle));
    }
} // namespace jnirt

#endif //JNI_RUNTIME_SUBSCRIPTION_H
