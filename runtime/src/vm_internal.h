// Runtime-internal access to the stored JavaVM.
//
// Created by Yurii Surzhykov on 9/30/26.

#ifndef JNIRT_RUNTIME_VM_INTERNAL_H
#define JNIRT_RUNTIME_VM_INTERNAL_H

#pragma once
#include <jni.h>

namespace jnirt::detail {
    // Like jnirt::vm(), but returns nullptr instead of crashing when setVm() was never called.
    JavaVM *vmOrNull() noexcept;
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_VM_INTERNAL_H
