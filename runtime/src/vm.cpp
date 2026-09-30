// The process-wide JavaVM.
//
// Created by Yurii Surzhykov on 9/30/26.

#include <jnirt/runtime/vm.h>
#include <jnirt/support/assert.h>

#include "vm_internal.h"

#include <atomic>

namespace jnirt {
    namespace {
        std::atomic<JavaVM *> gVm{nullptr};
    }

    void setVm(JavaVM *vm) {
        if (vm == nullptr) {
            JNIRT_FATAL("jnirt::setVm(nullptr).\n"
                "  why: JNI_OnLoad received no JavaVM, or a wrong pointer was passed on.\n"
                "  fix: pass the JavaVM* that JNI_OnLoad received to jnirt::init(vm, ...).");
        }
        JavaVM *expected = nullptr;
        if (gVm.compare_exchange_strong(expected, vm, std::memory_order_acq_rel)) return;
        if (expected == vm) return; // one more .so in the same process: same VM, nothing to do
        JNIRT_FATAL("two different JavaVM in one process (%p, then %p).\n"
                    "  why: jnirt::init was called with a JavaVM that is not the one it got first.\n"
                    "  fix: call jnirt::init(vm, ...) only from JNI_OnLoad, with the vm it receives.",
                    static_cast<void *>(expected), static_cast<void *>(vm));
    }

    JavaVM *vm() {
        JavaVM *current = gVm.load(std::memory_order_acquire);
        if (current == nullptr) {
            JNIRT_FATAL("jnirt is not initialized.\n"
                "  why: jnirt::vm() was called before jnirt::init(vm, ...).\n"
                "  fix: call jnirt::init(vm, ...) from JNI_OnLoad, see docs/integration.md.");
        }
        return current;
    }

    JavaVM *detail::vmOrNull() noexcept {
        return gVm.load(std::memory_order_acquire);
    }
} // namespace jnirt
