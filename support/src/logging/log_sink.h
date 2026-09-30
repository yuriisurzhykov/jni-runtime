//
// Created by Yurii Surzhykov on 9/28/26.
//

#ifndef JNI_RUNTIME_LOGSINK_H
#define JNI_RUNTIME_LOGSINK_H

#pragma once

#include <jnirt/support/logging/log.h>

namespace jnirt::logging {
    void sinkWrite(Level level, const char *tag, const char *message) noexcept;
}

#endif //JNI_RUNTIME_LOGSINK_H
