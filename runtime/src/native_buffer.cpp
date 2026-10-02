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
    } // namespace

    LocalRef<jobject> newNativeBuffer(JNIEnv *env, const NativeBuffer &buffer) {
        if (buffer.size() > static_cast<std::size_t>(std::numeric_limits<jint>::max())) {
            throw IllegalArgument("a ByteBuffer holds at most 2 GiB - 1 bytes, the native buffer has " +
                                  std::to_string(buffer.size()), "", 0);
        }
        void *address = buffer.size() == 0 ? static_cast<void *>(&gEmpty) : static_cast<void *>(buffer.data());
        if (address == nullptr) throw IllegalState("a NativeBuffer with a size has no data pointer", "", 0);

        const jlong handle = addPeer(buffer.owner(), &PeerTag<BufferMemory>::tag, nullptr);
        try {
            LocalRef<jobject> result(env, env->NewDirectByteBuffer(address, static_cast<jlong>(buffer.size())));
            if (!result) throwPendingException(env); // OutOfMemoryError
            NativeBuffersJ::watch(env, result.get(), handle); // from now on the buffer owns the handle
            return result;
        } catch (...) {
            PeerTable::close(handle);
            throw;
        }
    }
} // namespace jnirt::detail
