// A ByteBuffer over native memory, with the memory tied to the life of the buffer (ADR-0009 §9). The memory is
// registered in the table of peers as a peer without a hook, and Kotlin (NativeBuffers.watch) closes that handle when
// the garbage collector collects the ByteBuffer.
//
// Created by Yurii Surzhykov on 10/2/26.

#include <jnirt/runtime/exception.h>
#include <jnirt/runtime/native_buffer.h>
#include <jnirt/runtime/peer.h>
#include <jnirt/support/error.h>

#include "runtime_classes.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

namespace jnirt::detail {
    namespace {
        // The tag of the memory in the table: no object of this type ever exists.
        struct BufferMemory {
        };

        // NewDirectByteBuffer wants an address even for a buffer of no bytes.
        std::byte gEmpty;

        std::atomic<std::size_t> gLiveBytes{0};
        std::atomic<std::size_t> gThreshold{64u * 1024 * 1024};
        std::atomic<long> gForced{0};
        std::atomic<long long> gLastForcedMs{0};
        constexpr long long kMinMsBetweenCollections = 200;

        // The owner of the memory as the table keeps it: the size is counted while it lives.
        struct Counted {
            Counted(std::shared_ptr<void> owner, std::size_t size) : owner(std::move(owner)), size(size) {
                gLiveBytes += size;
            }

            ~Counted() { gLiveBytes -= size; }

            std::shared_ptr<void> owner;
            std::size_t size;
        };

        // Too much native memory waits for the collector, and the Java heap does not ask for one: ask.
        void nudgeCollector(JNIEnv *env) {
            if (gLiveBytes.load() <= gThreshold.load()) return;
            const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            long long last = gLastForcedMs.load();
            if (now - last < kMinMsBetweenCollections || !gLastForcedMs.compare_exchange_strong(last, now)) return;
            ++gForced;
            try {
                SystemJ::gc(env);
            } catch (const JavaException &) {
            }
        }
    } // namespace

    LocalRef<jobject> newNativeBuffer(JNIEnv *env, const NativeBuffer &buffer) {
        if (buffer.size() > static_cast<std::size_t>(std::numeric_limits<jint>::max())) {
            throw IllegalArgument("a ByteBuffer holds at most 2 GiB - 1 bytes, the native buffer has " +
                                  std::to_string(buffer.size()), "", 0);
        }
        void *address = buffer.size() == 0 ? static_cast<void *>(&gEmpty) : static_cast<void *>(buffer.data());
        if (address == nullptr) throw IllegalState("a NativeBuffer with a size has no data pointer", "", 0);

        const jlong handle = addPeer(std::make_shared<Counted>(buffer.owner(), buffer.size()),
                                     &PeerTag<BufferMemory>::tag, nullptr);
        try {
            LocalRef<jobject> result(env, env->NewDirectByteBuffer(address, static_cast<jlong>(buffer.size())));
            if (!result) throwPendingException(env); // OutOfMemoryError
            NativeBuffersJ::watch(env, result.get(), handle); // from now on the buffer owns the handle
            nudgeCollector(env);
            return result;
        } catch (...) {
            PeerTable::close(handle);
            throw;
        }
    }

    std::size_t liveBufferBytes() noexcept { return gLiveBytes.load(); }

    void setBufferGcThreshold(std::size_t bytes) noexcept { gThreshold.store(bytes); }

    long forcedCollections() noexcept { return gForced.load(); }
} // namespace jnirt::detail
