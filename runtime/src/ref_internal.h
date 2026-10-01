// Runtime-internal view of the reference bookkeeping behind <jnirt/runtime/ref.h>: read by detachCurrentThread()
// and by host tests. Not part of the public API.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_REF_INTERNAL_H
#define JNIRT_RUNTIME_REF_INTERNAL_H

#pragma once
#include <cstddef>

namespace jnirt::detail {
    // Live references made through jnirt. Always 0 in release builds.
    int liveLocalRefsOnThisThread() noexcept;
    std::size_t liveGlobalRefs() noexcept;
    std::size_t liveWeakGlobalRefs() noexcept;
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_REF_INTERNAL_H
