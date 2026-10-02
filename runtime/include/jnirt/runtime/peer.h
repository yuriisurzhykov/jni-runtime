// Peers: a Kotlin object that owns a native object. See docs/adr/0009-peers.md.
//
// The Kotlin object (jnirt.NativePeer) keeps a `Long` handle. Here the handle is looked up in one table, and the
// native object behind it is a shared_ptr: a native call holds it for the whole call, `close()` removes the handle and
// tells the object to stop (`onClose()`), and the memory goes when the last call lets go. In Kotlin terms the table is
// a ConcurrentHashMap<Long, Any> with a counter for the keys.
//
//   class Camera {                                    // plain C++, no JNI
//   public:
//       explicit Camera(const std::string &url);
//       std::vector<jbyte> read();                    // blocks until there is a frame
//       void onClose() noexcept;                      // optional: wake everyone who waits, do not wait yourself
//   };
//
//   Peer<Camera> open(JNIEnv *, jclass, std::string url) { return makePeer<Camera>(url); }   // Kotlin: Long
//   std::vector<jbyte> read(JNIEnv *, jclass, Peer<Camera> self) { return self->read(); }    // Kotlin: Long
//
// A `Peer<T>` result gets its handle on the way out; a `Peer<T>` parameter is looked up before the body runs, and
// held until it returns. A closed or unknown handle is an IllegalStateException in Kotlin, never a crash.
//
// Created by Yurii Surzhykov on 10/2/26.

#ifndef JNI_RUNTIME_PEER_H
#define JNI_RUNTIME_PEER_H

#pragma once
#include <jnirt/runtime/convert.h>

#include <jni.h>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace jnirt {
    // A native object that a native call holds: `->` and `*` as with a pointer. It is not a plain shared_ptr on
    // purpose: a shared_ptr in a signature must not silently become a handle.
    template<class T>
    class Peer {
    public:
        Peer() noexcept = default;

        explicit Peer(std::shared_ptr<T> object) noexcept : object_(std::move(object)) {
        }

        T *operator->() const noexcept { return object_.get(); }
        T &operator*() const noexcept { return *object_; }
        T *get() const noexcept { return object_.get(); }
        explicit operator bool() const noexcept { return object_ != nullptr; }

        const std::shared_ptr<T> &shared() const noexcept { return object_; }

    private:
        std::shared_ptr<T> object_;
    };

    namespace detail {
        // One address per type: the tag that tells a Camera handle from a Player handle, without RTTI.
        template<class T>
        struct PeerTag {
            static inline const char tag = 0;
        };

        // The hook of the class, found at compile time: `void onClose() noexcept`, or `void close() noexcept`.
        template<class T, class = void>
        struct HasOnClose : std::false_type {
        };

        template<class T>
        struct HasOnClose<T, std::void_t<decltype(std::declval<T &>().onClose())> > : std::true_type {
        };

        template<class T, class = void>
        struct HasClose : std::false_type {
        };

        template<class T>
        struct HasClose<T, std::void_t<decltype(std::declval<T &>().close())> > : std::true_type {
        };

        using PeerHook = void (*)(void *) noexcept;

        template<class T>
        PeerHook hookOf() {
            if constexpr (HasOnClose<T>::value) {
                static_assert(noexcept(std::declval<T &>().onClose()),
                              "jnirt: onClose() of a peer must be noexcept: it runs inside close(), which does not throw");
                return [](void *object) noexcept { static_cast<T *>(object)->onClose(); };
            } else if constexpr (HasClose<T>::value) {
                static_assert(noexcept(std::declval<T &>().close()),
                              "jnirt: close() of a peer must be noexcept: it runs inside close(), which does not throw");
                return [](void *object) noexcept { static_cast<T *>(object)->close(); };
            } else {
                return nullptr;
            }
        }

        // Defined in peer.cpp.
        jlong addPeer(std::shared_ptr<void> object, const void *tag, PeerHook hook);

        std::shared_ptr<void> acquirePeer(jlong handle, const void *tag);

        // A native returned an empty Peer: nothing to give a handle to. IllegalState.
        [[noreturn]] void nullPeer();
    } // namespace detail

    // The table of peers: one per process. Handles start at 1, are never reused, and 0 means "no peer".
    class PeerTable {
    public:
        // The object gets a new handle. Throws only std::bad_alloc.
        template<class T>
        static jlong add(std::shared_ptr<T> object) {
            return detail::addPeer(std::move(object), &detail::PeerTag<T>::tag, detail::hookOf<T>());
        }

        // The object behind the handle, held by the returned Peer until it is gone. IllegalState when the handle is
        // 0, unknown, closed, or the handle of an object of another type.
        template<class T>
        static Peer<T> acquire(jlong handle) {
            return Peer<T>(std::static_pointer_cast<T>(detail::acquirePeer(handle, &detail::PeerTag<T>::tag)));
        }

        // Removes the handle, calls the hook of the object (outside every lock), lets go of the table's reference.
        // The destructor runs here when no call is in flight, otherwise at the end of the last call. A handle that is
        // unknown or already closed does nothing, which is what makes Kotlin's close() idempotent.
        static void close(jlong handle) noexcept;

        // How many peers are open. For tests and diagnostics.
        static std::size_t size() noexcept;
    };

    // Constructs the object and wraps it. It gets its handle when it is the result of a native (see above); until then
    // it is only a C++ object, and an exception thrown on the way destroys it.
    template<class T, class... Args>
    Peer<T> makePeer(Args &&... args) {
        return Peer<T>(std::make_shared<T>(std::forward<Args>(args)...));
    }

    // The row of the table (convert.h): a Peer is a Long on the Java side.
    template<class T>
    struct Convert<Peer<T> > {
        using Jni = jlong;
        using Value = Peer<T>;
        static constexpr auto descriptor = detail::text("J");
        static constexpr bool ownsPeer = true;

        static Peer<T> fromJava(JNIEnv *, jlong handle) { return PeerTable::acquire<T>(handle); }

        static jlong toJava(JNIEnv *, const Peer<T> &peer) {
            if (!peer) detail::nullPeer();
            return PeerTable::add<T>(peer.shared());
        }
    };
} // namespace jnirt

#endif //JNI_RUNTIME_PEER_H
