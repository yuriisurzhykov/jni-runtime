// Internal: platform hook that stores the reason of an abort where a crash report can find it.
//
// Created by Yurii Surzhykov on 9/30/26.

#ifndef JNIRT_SUPPORT_ABORT_HOOK_H
#define JNIRT_SUPPORT_ABORT_HOOK_H

#pragma once

namespace jnirt::detail {
    void setAbortMessage(const char *message) noexcept;
}

#endif //JNIRT_SUPPORT_ABORT_HOOK_H
