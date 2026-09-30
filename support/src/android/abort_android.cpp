// Android: the text goes into the tombstone ("Abort message:").
//
// Created by Yurii Surzhykov on 9/30/26.

#include "abort_hook.h"

#include <android/set_abort_message.h>

namespace jnirt::detail {
    void setAbortMessage(const char *message) noexcept {
        android_set_abort_message(message);
    }
}
