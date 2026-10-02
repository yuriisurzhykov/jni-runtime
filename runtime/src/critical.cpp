// withCritical: the thread flag and the held array. See <jnirt/runtime/detail/convert_array.h> and
// docs/adr/0008-type-conversion.md §5.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/detail/convert_array.h>
#include <jnirt/support/assert.h>

#include "critical.h"

namespace jnirt::detail {
    namespace {
        // How many critical regions this thread is in (they may nest: two arrays at once).
        thread_local int gDepth = 0;
    } // namespace

    bool insideCritical() noexcept { return gDepth > 0; }

    CriticalArray::CriticalArray(JNIEnv *env, jarray array, bool readOnly)
        : env_(env), array_(array), data_(nullptr), size_(0), readOnly_(readOnly) {
        JNIRT_ASSERT(!insideCritical(),
                     "withCritical was called inside withCritical.\n"
                     "  why: between GetPrimitiveArrayCritical and its release no other JNI call is allowed, and the "
                     "second array needs one (GetArrayLength).\n"
                     "  fix: copy a piece with readArray / writeArray, or take the second array in a second pass.");
        requireArray(array);
        size_ = env->GetArrayLength(array);
        data_ = env->GetPrimitiveArrayCritical(array, nullptr);
        if (data_ == nullptr && size_ > 0) {
            // The JVM could not pin or copy the array: an OutOfMemoryError is pending.
            JNIRT_CHECK(env->ExceptionCheck(),
                        "GetPrimitiveArrayCritical returned null for an array of %d elements.\n"
                        "  why: the JVM could not pin or copy the array and did not say why.\n"
                        "  fix: none in the calling code; check memory usage.",
                        static_cast<int>(size_));
            throwPendingException(env);
        }
        ++gDepth; // only now: a constructor that throws leaves nothing behind
    }

    CriticalArray::~CriticalArray() {
        --gDepth;
        if (data_ != nullptr) env_->ReleasePrimitiveArrayCritical(array_, data_, readOnly_ ? JNI_ABORT : 0);
    }
} // namespace jnirt::detail
