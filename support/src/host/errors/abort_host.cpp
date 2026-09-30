// Host: there is no tombstone. The message was already printed by the log sink.
//
// Created by Yurii Surzhykov on 9/30/26.

#include "../../errors/abort_hook.h"

namespace jnirt::detail {
    void setAbortMessage(const char *) noexcept {
    }
}
