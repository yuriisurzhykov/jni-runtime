// Runtime-internal: is this thread between GetPrimitiveArrayCritical and its release? Read by env(), class_cache and the
// log forwarder, set by withCritical (detail::CriticalArray). Not part of the public API.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_CRITICAL_H
#define JNIRT_RUNTIME_CRITICAL_H

#pragma once

namespace jnirt::detail {
    // True while the current thread is inside withCritical / withCriticalRead.
    bool insideCritical() noexcept;
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_CRITICAL_H
