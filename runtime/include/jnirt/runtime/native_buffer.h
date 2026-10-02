// Native memory given to Kotlin as a direct ByteBuffer, without a copy. See docs/adr/0009-peers.md §9.
//
// A native function returns a NativeBuffer, and Kotlin gets a `java.nio.ByteBuffer` that looks at the same bytes:
//
//   NativeBuffer frame(JNIEnv *, jclass, Peer<Camera> self) { return self->nextFrame(); }   // Kotlin: ByteBuffer
//
// The memory belongs to a `shared_ptr` that the buffer carries. It is released when Kotlin has dropped the ByteBuffer
// and the garbage collector has collected it, not when the peer that made it is closed: closing the camera does not
// pull the memory from under a frame that Kotlin still holds. (If a frame must not outlive the camera, let the owner
// be the camera itself: then the memory goes after both.)
//
// A result of a native, or an argument of a call into Java (a frame handed to a listener, listener.h): a new buffer for
// every one. Not a parameter, a collection, an optional or a field. The bytes are big-endian when read as Int or Long, like every ByteBuffer; `order()` changes that.
// Kotlin must not write to a frame that native code still writes to; that is the library's contract, as with any
// shared memory.
//
// Created by Yurii Surzhykov on 10/2/26.

#ifndef JNI_RUNTIME_NATIVE_BUFFER_H
#define JNI_RUNTIME_NATIVE_BUFFER_H

#pragma once
#include <jnirt/runtime/convert.h>

#include <jni.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

namespace jnirt {
    class NativeBuffer {
    public:
        NativeBuffer() noexcept = default;

        // `owner` keeps the bytes alive (a vector, a pool slot, the camera); `data` points into them.
        NativeBuffer(std::shared_ptr<void> owner, std::byte *data, std::size_t size) noexcept
            : owner_(std::move(owner)), data_(data), size_(size) {
        }

        // `size` new bytes that C++ fills in through data() before the buffer is returned.
        static NativeBuffer allocate(std::size_t size) {
            std::shared_ptr<std::byte> memory(new std::byte[size == 0 ? 1 : size](), std::default_delete<std::byte[]>());
            std::byte *data = memory.get();
            return NativeBuffer(std::move(memory), data, size);
        }

        static NativeBuffer copyOf(const void *bytes, std::size_t size) {
            NativeBuffer buffer = allocate(size);
            if (size != 0) std::memcpy(buffer.data(), bytes, size);
            return buffer;
        }

        std::byte *data() const noexcept { return data_; }
        std::size_t size() const noexcept { return size_; }
        const std::shared_ptr<void> &owner() const noexcept { return owner_; }

    private:
        std::shared_ptr<void> owner_;
        std::byte *data_ = nullptr;
        std::size_t size_ = 0;
    };

    namespace detail {
        // Defined in native_buffer.cpp: the ByteBuffer over the bytes, with the memory tied to its life.
        LocalRef<jobject> newNativeBuffer(JNIEnv *env, const NativeBuffer &buffer);
    } // namespace detail

    // The row of the table: a NativeBuffer is a java.nio.ByteBuffer. A result, or an argument of a call into Java.
    template<>
    struct Convert<NativeBuffer> {
        using Jni = jobject;
        using Value = NativeBuffer;
        static constexpr auto descriptor = detail::text("Ljava/nio/ByteBuffer;");
        static constexpr bool ownsPeer = true; // like Peer: a handle is made on the way out
        static constexpr bool allowedAsArgument = true; // unlike Peer, also an argument of a call into Java (ADR-0010 §3)

        // Not a parameter: Kotlin's buffer is read with DirectBuffer, which borrows it for the call (ADR-0008 §3).
        template<class Never = void>
        static NativeBuffer fromJava(JNIEnv *, jobject) {
            static_assert(detail::AlwaysFalse<Never>::value,
                          "jnirt: a NativeBuffer is the result of a native function, not a parameter. To read a "
                          "ByteBuffer that Kotlin gives you, take a jnirt::DirectBuffer.");
            return {};
        }

        static LocalRef<jobject> toJava(JNIEnv *env, const NativeBuffer &buffer) {
            return detail::newNativeBuffer(env, buffer);
        }
    };
} // namespace jnirt

#endif //JNI_RUNTIME_NATIVE_BUFFER_H
