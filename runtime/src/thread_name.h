// The current thread's name, for attach and for error messages.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_THREAD_NAME_H
#define JNIRT_RUNTIME_THREAD_NAME_H

#pragma once
#include <sys/prctl.h>

namespace jnirt::detail {
    // The current thread's name as set by pthread_setname_np / prctl. pthread_getname_np needs API 26,
    // prctl works on every Android version. Up to 15 characters plus the terminating zero.
    struct ThreadName {
        char value[16] = {};

        ThreadName() noexcept { prctl(PR_GET_NAME, value, 0, 0, 0); }
    };
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_THREAD_NAME_H
