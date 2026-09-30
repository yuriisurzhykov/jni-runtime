//
// Created by Yurii Surzhykov on 9/28/26.
//

#ifndef JNIRT_SUPPORT_LOGGING_MACROS_H
#define JNIRT_SUPPORT_LOGGING_MACROS_H

#pragma once
#include "jnirt/support/logging/log.h"

#define LOG_DEFINE_TAG(tag) \
    namespace { [[maybe_unused]] constexpr const char* kSupportLogTag = tag; }

#define SUPPORT_LOG_IMPL(level, ...)                                        \
    do {                                                                    \
        if (::jnirt::logging::enabled(level))                             \
            ::jnirt::logging::write(level, kSupportLogTag, __VA_ARGS__);  \
    } while (0)

#define LOGV(...) SUPPORT_LOG_IMPL(::jnirt::logging::Level::Verbose, __VA_ARGS__)
#define LOGD(...) SUPPORT_LOG_IMPL(::jnirt::logging::Level::Debug,   __VA_ARGS__)
#define LOGI(...) SUPPORT_LOG_IMPL(::jnirt::logging::Level::Info,    __VA_ARGS__)
#define LOGW(...) SUPPORT_LOG_IMPL(::jnirt::logging::Level::Warn,    __VA_ARGS__)
#define LOGE(...) SUPPORT_LOG_IMPL(::jnirt::logging::Level::Error,   __VA_ARGS__)

#endif //JNIRT_SUPPORT_LOGGING_MACROS_H
